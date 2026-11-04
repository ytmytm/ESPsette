#!/usr/bin/env python3
"""Count the assembled unrolled receive paths and validate both timing profiles."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
# Opcode -> (length, base cycles). Indexed-table and branch pages checked below.
OPS = {
    0x05:(2,3), 0x0a:(1,2), 0x10:(2,2), 0x1d:(3,4), 0x20:(3,6),
    0x24:(2,3), 0x29:(2,2), 0x2c:(3,4), 0x45:(2,3), 0x49:(2,2),
    0x4a:(1,2), 0x4c:(3,3), 0x60:(1,6), 0x85:(2,3), 0x8d:(3,4),
    0x91:(2,6), 0x99:(3,5), 0xa2:(2,2), 0xa5:(2,3), 0xa9:(2,2),
    0xaa:(1,2), 0xad:(3,4), 0xc0:(2,2), 0xc6:(2,5), 0xc8:(1,2), 0xd0:(2,2),
    0xe6:(2,5), 0xf0:(2,2),
}
CLOCKS = {"c64":(985248,1022730), "c16":(1773447,1789772)}
PROFILES = {"c64":(16,19), "c16":(16,11)}


def analyze(c16, directory, boot=False):
    family = "c16" if c16 else "c64"
    prg, symbols = directory/f"{family}.prg", directory/f"{family}.labels"
    subprocess.run(["acme",f"-DC16={c16}",f"-DBOOT={int(boot)}",
                    f"-DORIGIN={0x0800 if c16 or not boot else 0x0400}","-f","cbm","-o",str(prg),
                    "-l",str(symbols),str(ROOT.parents[1]/"native/fastloader/receiver.asm")],check=True)
    raw = prg.read_bytes()
    origin = int.from_bytes(raw[:2],"little")
    memory = dict(enumerate(raw[2:],origin))
    labels = {name:int(value,16) for name,value in re.findall(
        r"\b(\w+)\s*=\s*\$([0-9a-f]+)",symbols.read_text())}
    assert (labels["upper_table"] & 255) + 15 < 256
    assert labels["receiver_end"] <= 0x1000

    def instruction(pc):
        op = memory[pc]
        size,cost = OPS[op]
        arg = sum(memory[pc+i] << (8*(i-1)) for i in range(1,size))
        return op,size,cost,arg

    def polls(start,end):
        result=[]
        pc=start
        while pc<end:
            op,size,_,arg=instruction(pc)
            if op in (0x24,0x2c) and arg==(1 if c16 else 0xdc0d): result.append(pc)
            pc+=size
        assert len(result)==4
        return result

    payload_polls=polls(labels["load"],labels["payload_store"])
    header_polls=polls(labels["header"],labels["header_store"])

    def walk(index, header=False):
        reads=header_polls if header else payload_polls
        start=reads[index]
        pc,cycles=start,0
        samples=[]
        pages=[]
        for step in range(200):
            if step and pc in reads: return cycles,samples,pages
            op,size,cost,arg=instruction(pc)
            following=pc+size
            if op in (0x10,0xd0,0xf0):
                target=following+(arg if arg<128 else arg-256)
                if target==start:
                    take=False  # Current strobe has arrived.
                elif target in (labels["load"],labels["header"]):
                    take=True
                else: raise AssertionError(f"Unexpected branch ${pc:04x} -> ${target:04x}")
                if take:
                    crossed=following>>8 != target>>8
                    cost+=1+crossed
                    if crossed: pages.append(pc)
                    following=target
            elif op in (0x20,0x60,0x4c):
                raise AssertionError("A per-byte path unexpectedly contains a call/return/jump")
            cycles+=cost
            if op in (0xa5,0x45) and arg==1 or op==0xad and arg==0xfd10:
                samples.append(cycles)
            pc=following
        raise AssertionError("Path did not reach another poll")

    def refresh(cycles): return 5*((cycles+108)//109) if c16 else 0
    pairs=[walk(index)[0] for index in range(3)]
    byte_paths=[dict(within_block=True,cycles=walk(3)[0])]
    poll_cycles=6 if c16 else 7
    read_cycles=3 if c16 else 4
    latest=[poll_cycles+max(walk(index)[1])-read_cycles for index in range(4)]
    pair_bound=max(c+refresh(c) for c in pairs)
    header_cycles=walk(3,header=True)[0]
    byte_bound=max([header_cycles+refresh(header_cycles)] +
                   [p["cycles"]+refresh(p["cycles"]) for p in byte_paths])
    data_bound=max(c+refresh(c) for c in latest)
    pages=[pc for i in range(4) for pc in walk(i)[2]]
    hold,pause=PROFILES[family]
    result=dict(machine=family,clocks_hz=dict(zip(("pal","ntsc"),CLOCKS[family])),
                poll_cycles=poll_cycles,pair_service_cycles=pairs,
                header_byte_service_cycles=header_cycles,payload_byte_paths=byte_paths,
                latest_data_sample_cycles=latest,branch_page_penalties=pages,
                pair_wall_bound_ticks=pair_bound,byte_wall_bound_ticks=byte_bound,
                latest_sample_wall_bound_ticks=data_bound,
                profile_us=dict(setup=3,hold=hold,gap=3,byte_pause=pause),
                receiver_sha256=hashlib.sha256(raw).hexdigest(),receiver_bytes=len(raw)-2)
    margins={}
    for video,hz in result["clocks_hz"].items():
        ticks=lambda us:(us*hz+999999)//1000000
        pair_ticks=ticks(hold)+2*ticks(3)
        byte_ticks=ticks(hold)+ticks(pause)+ticks(3)
        data_ticks=ticks(hold)+ticks(3)
        pair_real=(hold+6)*hz/1000000
        byte_real=(hold+pause+3)*hz/1000000
        data_real=(hold+3)*hz/1000000
        assert pair_ticks>=pair_bound+2
        assert byte_ticks>=byte_bound+3
        assert data_ticks>=data_bound+2
        assert pair_real>=pair_bound+1.5
        assert byte_real>=byte_bound+2.5
        assert data_real>=data_bound+2.5
        margins[video]=dict(pair_ticks=pair_ticks,byte_ticks=byte_ticks,
                           pair_margin=pair_ticks-pair_bound,byte_margin=byte_ticks-byte_bound,
                           data_hold_margin=data_ticks-data_bound,
                           pair_margin_without_rounding=round(pair_real-pair_bound,3),
                           byte_margin_without_rounding=round(byte_real-byte_bound,3),
                           data_margin_without_rounding=round(data_real-data_bound,3))
    result["profile_margins_ticks"]=margins
    return result


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path)
    parser.add_argument("--boot",action="store_true",help="Analyze the low-RAM bootstrap receiver")
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="ESPsette-timing-") as directory:
        results=[analyze(c16,Path(directory),args.boot) for c16 in (0,1)]
    output=json.dumps(results,indent=2)+"\n"
    print(output,end="")
    if args.out:
        args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(output)
