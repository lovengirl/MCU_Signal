"""TI_MCU nominal stimulus / schematic bridge. Python standard library only."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
COMMIT = '869e4cc84f9586cf94acd9b9e71ced40c42c6fc3'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(c):
    if c['schema_version'] != 1:
        raise ValueError('Unsupported schema version')
    for relative, expected in c['provenance']['sha256'].items():
        if digest(ROOT / 'reference' / relative) != expected:
            raise ValueError('Reference FW changed: ' + relative)
    signals = c['signals']
    if c['device'] != 'MSPM0G3507SPMR' or c['provenance']['commit'] != COMMIT:
        raise ValueError('Device or firmware revision differs from the verified reference')
    if any(s['initial'] != 0 for s in signals):
        raise ValueError('This FW boots with all supported signals zero')
    names = [s['net'] for s in signals]
    if len(signals) != 13 or len(set(names)) != 13:
        raise ValueError('Expected 13 unique signals')
    for key in ('pin', 'port', 'fw_alias'):
        if len({s[key] for s in signals}) != 13:
            raise ValueError('Duplicate ' + key)
    required = {f'{g}_ADDR{i}' for g in ('TX', 'RX') for i in range(5)} | {'TX_EN', 'RX_EN', 'LIGHT_SET'}
    if set(names) != required:
        raise ValueError('Unexpected signal names')
    # These identities are verified against the pinned board.syscfg and reference pin table.
    for s in signals:
        n = s['net']
        if '_ADDR' in n:
            group, bit = n.split('_ADDR'); bit = int(bit)
            expected = (f'PB{bit if group == "TX" else bit+6}', [47,48,50,51,52][bit] if group == 'TX' else [58,59,60,61,62][bit], f'{group}_A{bit}')
        else:
            expected = {'TX_EN': ('PB5',53,'TX_EN'), 'RX_EN': ('PB11',63,'RX_EN'), 'LIGHT_SET': ('PA15',8,'DAC_OUT')}[n]
        if (s['port'], s['pin'], s['fw_alias']) != expected:
            raise ValueError('Pin remap requires FW/reference review: ' + n)
    t = c['timing_ns']; e = c['electrical']; codes = c['dac_codes']
    for name, v in t.items():
        if type(v) is not int or v < 0:
            raise ValueError('Timing must be nonnegative integer ns: ' + name)
    if not 5000 <= t['select'] <= 10000000 or not 5000 <= t['light'] <= 10000000 or not 10000 <= t['sample_period'] <= 1000000:
        raise ValueError('Timing outside FW sensor_config_valid bounds')
    if t['adc_acquire'] != 2000 or t['adc_acquire'] + t['adc_convert_assumed'] >= t['sample_period']:
        raise ValueError('ADC timing invalid for this reference')
    if t['io_step_assumed'] < max(e['rise_ns'], e['fall_ns']):
        raise ValueError('io_step_assumed must cover digital edges')
    if t['frame_gap_assumed'] < e['dac_transition_ns']:
        raise ValueError('Frame gap must cover final DAC settling ramp')
    if len(codes) != 20 or any(type(x) is not int or not 0 <= x <= 4095 for x in codes):
        raise ValueError('Expected 20 DAC codes in 0..4095')
    for k,v in e.items():
        if not isinstance(v,(int,float)) or not math.isfinite(v) or v <= 0:
            raise ValueError('Electrical values must be finite and positive: ' + k)
    if c['scenario']['frames'] < 1 or type(c['scenario']['frames']) is not int:
        raise ValueError('frames must be a positive integer')
    if c['scenario']['frames'] > 100:
        raise ValueError('Maximum 100 frames per generated deck')
    # Ensure nominal settings stay tied to the checked-in FW, not README prose alone.
    src = (ROOT/'reference/src/sensor.c').read_text()
    for fragment in ('c->select_us=50', 'c->light_us=50', 'c->sample_us=10', 's->hal.tx(false);s->hal.rx(false);s->hal.select(s->channel)', 's->hal.dac(s->config.dac[s->channel]);s->hal.rx(true)'):
        if fragment not in src:
            raise ValueError('Reference state-machine contract changed')


def timeline(c):
    """Nominal discrete-event replay of sensor.c; not a CPU/peripheral emulator."""
    validate(c)
    events=[]; samples=[]; states={s['net']:0 for s in c['signals']}
    for n in states:
        events.append({'time_ns':0,'net':n,'value':0,'frame':0,'channel':-1,'reason':'safe boot'})
    def emit(time, net, value, frame, ch, why):
        if states[net] != value:
            states[net]=value
            events.append(dict(time_ns=time,net=net,value=value,frame=frame,channel=ch,reason=why))
    t=c['timing_ns']; step=t['io_step_assumed']; now=t['start_delay_assumed']
    for f in range(1,c['scenario']['frames']+1):
        for ch in range(20):
            emit(now,'TX_EN',0,f,ch,'channel_start: tx(false)'); now+=step
            emit(now,'RX_EN',0,f,ch,'channel_start: rx(false)'); now+=step
            for g in ('TX','RX'):
                for b in range(5):
                    emit(now,f'{g}_ADDR{b}',(ch>>b)&1,f,ch,'single masked GPIO write')
            now+=step
            emit(now,'LIGHT_SET',c['dac_codes'][ch],f,ch,'DAC code'); now+=step
            emit(now,'RX_EN',1,f,ch,'channel_start: rx(true)'); now+=step
            now+=t['select']+t['main_latency_assumed']
            for phase in ('OFF','ON'):
                start=now
                for k in range(4):
                    trigger=start+(k+1)*t['sample_period']
                    samples.append(dict(frame=f,channel=ch,phase=phase,index=k,trigger_ns=trigger,acquire_end_ns=trigger+t['adc_acquire'],result_ns=trigger+t['adc_acquire']+t['adc_convert_assumed']))
                now=samples[-1]['result_ns']+t['dma_latency_assumed']
                emit(now,'TX_EN',0,f,ch,'DMA ISR: tx(false)')
                now+=t['main_latency_assumed']
                if phase=='OFF':
                    emit(now,'TX_EN',1,f,ch,'OFF processed: tx(true)')
                    now+=step+t['light']+t['main_latency_assumed']
        emit(now,'TX_EN',0,f,19,'frame safe'); now+=step
        emit(now,'RX_EN',0,f,19,'frame safe'); now+=step
        emit(now,'LIGHT_SET',0,f,19,'frame safe DAC zero')
        now+=t['frame_gap_assumed']
    return sorted(events,key=lambda x:x['time_ns']),samples,now


def points(c, net, events, stop):
    e=c['electrical']; dac=net=='LIGHT_SET'
    def volts(v):
        return v*e['dac_vref_v']/4096.0 if dac else v*e['logic_high_v']
    out=[(0,0.0)]; previous=0.0
    for ev in events:
        if ev['net']!=net or ev['time_ns']==0:
            continue
        v=volts(ev['value']); time=ev['time_ns']
        duration=e['dac_transition_ns'] if dac else (e['rise_ns'] if v>previous else e['fall_ns'])
        if time < out[-1][0]:
            raise ValueError('Overlapping PWL edges: ' + net)
        if time > out[-1][0]: out.append((time,previous))
        out.append((time+duration,v)); previous=v
    if stop<=out[-1][0]: raise ValueError('Stop time overlaps last edge')
    out.append((stop,previous))
    return out


def write_csv(path, rows):
    with path.open('w',newline='',encoding='utf-8') as fp:
        w=csv.DictWriter(fp,fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)


def altium(c):
    assignments=[]
    for i,s in enumerate(c['signals']):
        assignments.append(f"    Pins[{i}] := '{s['pin']}'; Nets[{i}] := '{s['net']}';")
    template=(ROOT/'templates/apply_signals.pas.in').read_text()
    return template.replace('@ASSIGNMENTS@','\n'.join(assignments))


def generate(c,out):
    events,samples,stop=timeline(c); out.mkdir(parents=True,exist_ok=True)
    write_csv(out/'events.csv',events); write_csv(out/'adc_windows.csv',samples)
    write_csv(out/'pinmap.csv',c['signals'])
    lines=['* TI_MCU nominal behavioral stimulus; NOT an MCU/IBIS model', '* Source FW '+COMMIT, '* Pins order: '+ ' '.join(s['net'] for s in c['signals'])+' VSS', '.SUBCKT TI_MCU_STIM '+ ' '.join(s['net'] for s in c['signals'])+' VSS']
    wave={}
    for s in c['signals']:
        n=s['net']; pts=points(c,n,events,stop); wave[n]=pts
        lines.append('VDRV_'+n+' NDRV_'+n+' VSS PWL(')
        lines.extend('+ %.12g %.12g'%(tm*1e-9,v) for tm,v in pts)
        lines.append('+ )')
        r=c['electrical']['dac_rout_ohm' if n=='LIGHT_SET' else 'digital_rout_ohm']
        lines.append(f'ROUT_{n} NDRV_{n} {n} {r:.12g}')
    lines.append('.ENDS TI_MCU_STIM')
    (out/'mcu_sources.lib').write_text('\n'.join(lines)+'\n',encoding='ascii')
    ports=' '.join(s['net'] for s in c['signals'])
    demo=['TI_MCU stimulus smoke-test circuit', '.INC "mcu_sources.lib"', f'XMCU {ports} 0 TI_MCU_STIM']
    for s in c['signals']:
        n=s['net']; demo.extend([f'RLOAD_{n} {n} 0 1G',f'CLOAD_{n} {n} 0 5p'])
    demo+=['* Placeholder loads only; replace with the actual product circuit.', f'.TRAN 1u {stop*1e-9:.12g} 0 1u', '.PROBE', '.END']
    (out/'demo.cir').write_text('\n'.join(demo)+'\n',encoding='ascii')
    (out/'apply_signals.pas').write_text(altium(c),encoding='ascii')
    (out/'SignalBridge.PrjScr').write_text('[Design]\nVersion=1.0\n\n[Document1]\nDocumentPath=apply_signals.pas\n',encoding='ascii')
    (out/'waveforms.json').write_text(json.dumps(wave,indent=2)+'\n')
    report={'firmware_commit':COMMIT,'model':'nominal state-machine replay, not measured timing','signals':13,'frames':c['scenario']['frames'],'adc_samples':len(samples),'stop_ns':stop,'assumptions':c['assumptions'],'config_sha256':hashlib.sha256(json.dumps(c,sort_keys=True).encode()).hexdigest()}
    (out/'generation_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'definition.resolved.json').write_text(json.dumps(c,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config',type=Path,default=ROOT/'signals.json')
    p.add_argument('--out',type=Path,default=ROOT/'generated')
    args=p.parse_args()
    try:
        c=json.loads(args.config.read_text(encoding='utf-8'))
        print(json.dumps(generate(c,args.out),ensure_ascii=False,indent=2))
    except (ValueError,KeyError,TypeError,OSError) as error:
        p.exit(2,'Generation failed: '+str(error)+'\n')


if __name__=='__main__': main()
