import sys
def main(args):
    delay_reordered = {}
    delay_counts = {}
    for fname in args:
        f = open(fname)
        for line in f:
            splits = line.strip().split(",")
            splits = map(float, splits)
            if splits[1] != 0:
                delay_reordered[splits[0]] = delay_reordered.get(splits[0], 0.0) + 1
            else:
                delay_reordered[splits[0]] = delay_reordered.get(splits[0], 0.0) + 0
            delay_counts[splits[0]] = delay_counts.get(splits[0], 0) + 1
    for k in sorted(delay_counts.keys()):
        print str.format("{0},{1}", k, delay_reordered[k] / delay_counts[k])
if __name__ == "__main__":
    main(sys.argv[1:])
