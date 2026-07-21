.PHONY: all run run-headless clean check

all:
	./scripts/build.sh

run: all
	./scripts/run.sh

run-headless: all
	./scripts/run.sh --headless

check:
	./scripts/check.sh

clean:
	rm -rf build

