import sys
def main(args):
    sums = {}
    delay_counts = {}
    for fname in args:
        f = open(fname)
        for line in f:
            splits = line.strip().split(",")
            splits = map(float, splits)
            sums[splits[0]] = map(lambda x, y: x+ y, splits[1:], sums.get(splits[0], [0 for x in splits[1:]]))
            delay_counts[splits[0]] = delay_counts.get(splits[0], 0) + 1
    for k in sorted(delay_counts.keys()):
        print str.format("{0},{1}", k, ','.join(map(str, map(lambda x: x / delay_counts[k], sums[k]))))
if __name__ == "__main__":
    main(sys.argv[1:])
