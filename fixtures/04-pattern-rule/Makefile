SOURCES := red.txt blue.txt
COPIES := $(SOURCES:%.txt=build/%.copy)

all: $(COPIES)

build/%.copy: %.txt
	@mkdir -p build
	@cp $< $@

.PHONY: all clean
clean:
	@rm -rf build

