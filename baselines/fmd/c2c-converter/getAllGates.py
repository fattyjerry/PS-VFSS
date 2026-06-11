import sys


def main(fname):
    allOps = {}
    with open(fname, "r") as fileobj:
        for line in fileobj.readlines():
            stripped = line.strip().split(" ")[-1]
            if stripped != "IN" and stripped != "OUT":
                if stripped in allOps: 
                    allOps[stripped] += 1
                else: 
                    allOps[stripped] = 1
    print(allOps)


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1].split(".")[1] != "circ":
        print("Format: python3 {} [file ending in circ]".format(sys.argv[0]))
        sys.exit()
    main(sys.argv[1])
