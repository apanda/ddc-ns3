import sys
from os import path
if __name__=="__main__":
    if len(sys.argv) < 2:
        print "usage: <script> <result>"
        sys.exit(1)
    results = open(path.expanduser(sys.argv[1]))
    summed_stretches = {}
    stretch_sizes = {}
    for line in results:
        parts = line.split(',')
        if parts[0] not in stretch_sizes:
            summed_stretches[parts[0]] = map(float, parts[1:])
            stretch_sizes[parts[0]] = map(lambda a: 1 if float(a) != 0.0 else 0, parts[1:])
        else:
            summed_stretches[parts[0]] = map(lambda s1, s2: s1 + (s2 if s2 is not None else 0) , summed_stretches[parts[0]], map(float, parts[1:]))
            stretch_sizes[parts[0]] = map(lambda a, b: b + (1 if a is not None and float(a) != 0.0  else 0), parts[1:], stretch_sizes[parts[0]])
    for k in sorted(summed_stretches.keys()):
        packets = []
        for i in xrange(0, len(summed_stretches[k]), 2):
            packets.append((summed_stretches[k][i] + summed_stretches[k][i + 1]) / float(stretch_sizes[k][i] + stretch_sizes[k][i + 1]))
        print str.format("# {0} failures", k)
        for i in xrange(0, len(packets)):
            print str.format("{0},{1}", i + 1, packets[i])
        print
        print

