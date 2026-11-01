import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import bridge


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.c=json.loads((bridge.ROOT/'signals.json').read_text(encoding='utf-8'))

    def test_fw_reference_integrity(self):
        bridge.validate(self.c)
        self.c['provenance']['sha256']['src/sensor.c']='0'*64
        with self.assertRaisesRegex(ValueError,'Reference FW changed'): bridge.validate(self.c)

    def test_address_sequence_and_blank_before_selection(self):
        ev,_,_=bridge.timeline(self.c)
        state={s['net']:0 for s in self.c['signals']}
        seen=[]
        for x in ev:
            if '_ADDR' in x['net'] and x['time_ns']:
                self.assertEqual(state['TX_EN'],0)
                self.assertEqual(state['RX_EN'],0)
            state[x['net']]=x['value']
            if x['net']=='RX_EN' and x['value']==1:
                tx=sum(state[f'TX_ADDR{b}']<<b for b in range(5))
                rx=sum(state[f'RX_ADDR{b}']<<b for b in range(5))
                self.assertEqual(tx,rx); seen.append(tx)
        self.assertEqual(seen,list(range(20)))

    def test_adc_count_interval_apertures_and_tx_state(self):
        ev,sm,_=bridge.timeline(self.c)
        self.assertEqual(len(sm),160)
        for ch in range(20):
            for phase in ('OFF','ON'):
                block=[x for x in sm if x['channel']==ch and x['phase']==phase]
                self.assertEqual([block[i+1]['trigger_ns']-block[i]['trigger_ns'] for i in range(3)],[10000]*3)
                for x in block:
                    tx=[e for e in ev if e['net']=='TX_EN' and e['time_ns']<=x['trigger_ns']][-1]['value']
                    self.assertEqual(tx,int(phase=='ON'))
                    self.assertEqual(x['acquire_end_ns']-x['trigger_ns'],2000)
        self.assertEqual(sum(x['net']=='TX_EN' and x['value']==1 for x in ev),20)

    def test_final_safe_and_multiple_frames(self):
        self.c['scenario']['frames']=2
        ev,sm,_=bridge.timeline(self.c)
        self.assertEqual(len(sm),320)
        for n in ('TX_EN','RX_EN','LIGHT_SET'):
            self.assertEqual([x for x in ev if x['net']==n][-1]['value'],0)

    def test_dac_voltage_and_monotonic_pwl(self):
        self.c['dac_codes']=list(range(0,4000,200))
        ev,_,stop=bridge.timeline(self.c)
        for s in self.c['signals']:
            pts=bridge.points(self.c,s['net'],ev,stop)
            self.assertTrue(all(a[0]<b[0] for a,b in zip(pts,pts[1:])))
        vals=[v for _,v in bridge.points(self.c,'LIGHT_SET',ev,stop)]
        self.assertIn(3800*3.3/4096,vals)

    def test_invalid_input_rejected(self):
        for mutate in (lambda c:c['signals'][0].update(pin=53),lambda c:c.update(dac_codes=[4096]*20),lambda c:c['timing_ns'].update(sample_period=1000),lambda c:c['electrical'].update(rise_ns=-1),lambda c:c['timing_ns'].update(frame_gap_assumed=0)):
            c=copy.deepcopy(self.c);mutate(c)
            with self.assertRaises(ValueError):bridge.validate(c)

    def test_spice_generated_ports_and_sources(self):
        with tempfile.TemporaryDirectory(dir=bridge.ROOT/'tests') as d:
            out=Path(d);bridge.generate(self.c,out)
            lib=(out/'mcu_sources.lib').read_text()
            self.assertEqual(sum(x.startswith('VDRV_') for x in lib.splitlines()),13)
            self.assertEqual(sum(x.startswith('ROUT_') for x in lib.splitlines()),13)
            self.assertNotIn('.END\n',lib)
            self.assertEqual(lib.count('.SUBCKT'),1)
            for s in self.c['signals']:
                self.assertIn(' '+s['net']+' ',lib)
            script=(out/'apply_signals.pas').read_text()
            self.assertNotIn('@ASSIGNMENTS@',script)
            self.assertIn('procedure AuditSignals',script)


if __name__=='__main__':unittest.main(verbosity=2)
