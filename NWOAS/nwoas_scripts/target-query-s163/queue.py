#!/usr/bin/env python3
"""Queue bounded read-only target action queries for an enabled running launcher."""
import argparse,json,uuid
from pathlib import Path
a=argparse.ArgumentParser()
a.add_argument('link_dir',type=Path)
a.add_argument('actions',nargs='+',type=int,choices=range(3,11))
args=a.parse_args()
if not args.link_dir.is_dir() or len(args.actions)>8:
    a.error('existing link directory and at most8actions required')
request={'id':str(uuid.uuid4()),'actions':args.actions}
temp=args.link_dir/'target-query.tmp'
temp.write_text(json.dumps(request)+'\n')
temp.replace(args.link_dir/'target-query.json')
print(json.dumps(request))
