import sys
if len(sys.argv) < 2:
  print >>sys.stderr, "Usages %s fname"%(sys.argv[0])
  sys.exit(1)
switches = {}
topo = open(sys.argv[1])
for l in topo:
    parts = map(int, l.split()[:2])
    for part in parts:
        switches[part] = True
    print l.strip()
switches = sorted(switches.keys())
host_start = switches[-1] + 1
for switch in switches:
    print "%d %d h"%(switch, host_start)
    host_start+=1
