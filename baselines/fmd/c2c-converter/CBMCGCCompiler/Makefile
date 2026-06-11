CBMC_GC = cbmc-gc
CIRCUIT_SIM = circuit-sim

.PHONY: clean run-sim

output.gate.txt: main.c
	$(CBMC_GC) $^ --minimization-time-limit 10

run-sim:
	@$(CIRCUIT_SIM) --spec-file test.spec

clean:
	rm -f output.*.txt
