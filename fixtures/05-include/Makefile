include words.mk

all: message.txt

message.txt:
	@printf '%s\n' '$(WORDS)' > message.txt

.PHONY: all clean
clean:
	@rm -f message.txt

