import sys
import random
def invTransformSampling(cdf):
  u = random.random()
  for v, p in cdf:
    if p > u:
      return v
  return cdf[-1][0]
if len(sys.argv) < 4:
  print "Usage: %s topology nlinks nflows"%(sys.argv[0])
  sys.exit(1)
topo = open(sys.argv[1])
nlinks = int(sys.argv[2])
nflows = int(sys.argv[3])
hosts = []
links = []
for l in topo:
  p = l.split(' ')
  if len(p) > 2:
    hosts.append(int(p[1]))
  else:
    links.append((int(p[0]), int(p[1])))
schedule = []
print >>sys.stderr, len(links), nlinks
# link fails at 0.5 second
realtime = 0.0
gen = 0l
fail_links = random.sample(links, nlinks)
schedule.append((1.5, 'f', ' '.join(map(lambda l: '='.join(map(str, l)), fail_links))))
for flow in xrange(0, nflows):
  client = random.choice(hosts)
  server = random.choice(filter(lambda h: h != client, hosts))
  flow_len = 1073741824
  schedule.append((1.0, 'q', client, server, flow_len))

#for link in xrange(0, nlinks):
#  time = invTransformSampling(link_fail_cdf)
#  link = [random.choice(links)]
#  schedule.append((time, 'f', ' '.join(map(lambda l: '='.join(map(str, l)), link))))
#  for flow in xrange(0, nflows):
#    client = random.choice(hosts)
#    server = random.choice(filter(lambda h: h != client, hosts))
#    flow_len = 209715200
#    schedule.append((time, 'q', client, server, flow_len))


schedule = sorted(schedule, key = lambda e: e[0])
for event in schedule:
  print ' '.join(map(str, event))
