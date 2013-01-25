import sys
import random
def invTransformSampling(cdf):
  u = random.random()
  for v, p in cdf:
    if p > u:
      return v
  return cdf[-1][0]
if len(sys.argv) < 5:
  print "Usage: %s topology query_interarrival_distribution fanout linkrepair-cdf"%(sys.argv[0])
topo = open(sys.argv[1])
dist = open(sys.argv[2])
fanout = int(sys.argv[3])
cdf = map(lambda x: tuple(map(float, x.split(','))), dist.readlines())
dist = open(sys.argv[4])
link_cdf = map(lambda x: tuple(map(float, x.split(','))), dist.readlines())
hosts = []
links = []
for l in topo:
  p = l.split(' ')
  if len(p) > 2:
    hosts.append(int(p[1]))
  else:
    links.append((int(p[0]), int(p[1])))
schedule = []
link_group_cdf = [
  (1, 0.6),
  (2, 0.7),
  (3, 0.8),
  (5, 0.9)
]
# link fails at 1.0 second
realtime = 0.0
gen = 0l
link_failtime = 50.0
link_repairtime = link_failtime + invTransformSampling(link_cdf)
links_failed = invTransformSampling(link_group_cdf)
end = link_repairtime + link_failtime
link = random.sample(links, links_failed)
schedule.append((link_failtime, 'f', ' '.join(map(lambda l: '='.join(map(str, l)), link))))
while realtime < end:
  time = invTransformSampling(cdf)
  client = random.choice(hosts)
  servers = random.sample(filter(lambda h: h != client, hosts), fanout)
  realtime += time
  schedule.append((realtime, 'q', client, ' '.join(map(str, servers))))
  gen += 1l
schedule = sorted(schedule, key = lambda e: e[0])
for event in schedule:
  print ' '.join(map(str, event))
