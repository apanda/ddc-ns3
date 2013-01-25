import sys
if len(sys.argv) < 2:
  print >>sys.stderr, "Usages %s arity"%(sys.argv[0])
  sys.exit(1)
arity = int(sys.argv[1])
ncore_switches = (arity / 2) ** 2
pods = arity
switches_assigned_so_far = ncore_switches
for p in xrange(0, pods):
  # Agg singly connected to core
  aggs = []
  for agg in xrange(0, arity / 2):
    aggs.append(agg + switches_assigned_so_far)
    for agg_connect in xrange(0, arity / 2):
      print "%d %d"%((arity/2)*agg + agg_connect, agg + switches_assigned_so_far)
  switches_assigned_so_far += (arity/2)
  tors = []
  for tor in xrange(0, arity/2):
    tors.append(tor + switches_assigned_so_far)
    for agg in aggs:
      print "%d %d"%((agg, tor + switches_assigned_so_far))
  switches_assigned_so_far += (arity/2)
  for tor in tors:
    for host in xrange(0, arity / 2):
      print "%d %d h"%(tor, host + switches_assigned_so_far)
    switches_assigned_so_far += (arity / 2)

