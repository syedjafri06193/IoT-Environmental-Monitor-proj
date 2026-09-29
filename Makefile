# Top-level: run every test suite in the repository.
.PHONY: test sim clean

test:
	$(MAKE) -C firmware test
	node --test decoder/tests/*.test.js
	python3 -m pytest -q tools/tests server/bridge/tests
	python3 tools/bom_check.py hardware/bom.csv

sim:
	$(MAKE) -C firmware sim

clean:
	$(MAKE) -C firmware clean
