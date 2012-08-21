import sys
import numpy
def main(args):
    sums = {}
    delay_counts = {}
    for fname in args:
        f = open(fname)
        for line in f:
            splits = line.strip().split(",")
            splits = map(float, splits)
            if splits[0] not in sums:
                sums[splits[0]] = [[] for x in splits[1:]]
            map(lambda x, y: y.append(x), splits[1:], sums[splits[0]])
            delay_counts[splits[0]] = delay_counts.get(splits[0], 0) + 1
    for k in sorted(delay_counts.keys()):
        print str.format("{0},{1}", k, ','.join(map(str, reduce(list.__add__, map(lambda x: [numpy.mean(x), numpy.std(x), numpy.amax(x)], sums[k])))))
if __name__ == "__main__":
    main(sys.argv[1:])
