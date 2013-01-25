import sys
if len(sys.argv) < 2:
  print "%s log"%(sys.argv[0])
  sys.exit(1)

f = open(sys.argv[1])
requests = {}
unique_requests = {}
for l in f:
  if "Request" not in l:
    continue
  if "Request issued" in l:
    l = l.split(' ')
    t = tuple(map(int, l[2:5]))
    requests[t] = requests.get(t, 0) + 1
  elif "Request fulfilled" in l:
    l = l.split(' ')
    t = tuple(map(int, l[2:5]))
    requests[t] -= 1
for k in sorted(requests.iterkeys(), key = lambda k: k[0]):
  v = requests[k]
  if v > 0:
   print k, v
   unique_requests[k[0]] = unique_requests.get(k[0], 0) + 1
print len(unique_requests.keys()), max(unique_requests.values()), sorted(unique_requests.values(), reverse = True)[:100]
#for k, v in unique_requests.iteritems():
#  print k , v
