# Top-level convenience wrapper. The real build rules live in host/ and guest/.

.PHONY: all host guest clean test demo-a demo-b demo-c

all: host guest

host:
	$(MAKE) -C host

guest:
	$(MAKE) -C guest

test: all
	./scripts/run_tests.sh

demo-a: all
	./scripts/demo_a.sh

demo-b: all
	./scripts/demo_b.sh

demo-c: all
	./scripts/demo_c.sh

clean:
	$(MAKE) -C host clean
	$(MAKE) -C guest clean
	rm -rf vm_* input.txt shared.txt shared.txt.orig
