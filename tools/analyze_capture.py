"""Conservative host analysis of captured envelopes; never assumes remote model.
BOSCH144 facts/test vector source: IRremoteESP8266 ir_Bosch.{h,cpp} and tests.
https://github.com/crankyoldgit/IRremoteESP8266 (LGPL-2.1 upstream).
This analyzer is not a transmitter or a claim of rn10l2b2hs/bgh compatibility.
"""
import argparse
import json
from pathlib import Path

def decode_bosch(timings):
    if len(timings) not in (199,299):return None
    data=[]
    for section in range((len(timings)+1)//100):
        at=section*100
        if not (3500<=timings[at]<=5500 and 3500<=timings[at+1]<=5500):return None
        for byte in range(6):
            value=0
            for bit in range(8):
                index=at+2+2*(8*byte+bit)
                mark,space=timings[index:index+2]
                if not 250<=mark<=850:return None
                if 350<=space<=950 and 800<=mark+space<=1450:digit=0
                elif 1100<=space<=2100 and 1700<=mark+space<=2700:digit=1
                else:return None
                value=(value<<1)|digit
            data.append(value)
        if not 250<=timings[at+98]<=850:return None
        if at+99<len(timings) and not 4000<=timings[at+99]<=7000:return None
    if any((data[i]^data[i+1])!=255 for i in range(0,12,2)):return None
    if data[:6]!=data[6:12] or data[0]!=0xB2:return None
    if len(data)==12:
        if data!=[0xB2,0x4D,0x7B,0x84,0xE0,0x1F]*2:return None
        return {'protocol':'BOSCH144-family off96','bytes':data,'power':False}
    if data[12]!=0xD5 or (sum(data[12:17])&255)!=data[17]:return None
    code=((data[4]>>4)<<2)|((data[15]&16)>>3)|((data[14]&32)>>5)
    c_map=[2,0,4,12,8,24,28,20,16,48,52,36,32,40,44]
    temp=None
    if not data[15]&1 and code&~1 in c_map:
        temp=16+c_map.index(code&~1)+(0.5 if code&1 else 0)
    return {'protocol':'BOSCH144','bytes':data,'power':True,
            'temperature_c':temp,'temperature_code':code,
            'mode_code':(((data[4]>>2)&3)<<1)|(data[13]&1),
            'fan_code':((data[2]>>5)<<6)|((data[13]>>1)&63),
            'model_compatibility':'not established by decoder alone'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path)
    a=p.parse_args();record=json.loads(a.capture.read_text())
    t=record['timings_us'];decoded=decode_bosch(t)
    print(json.dumps({'timing_count':len(t),'duration_us':sum(t),'decoded':decoded,
                      'status':'validated framing/complements/checksum' if decoded else 'unclassified; no protocol forced'},indent=2))
if __name__=='__main__':main()
