OUTPUT := message.txt
MESSAGE = variables are data

all: $(OUTPUT)

$(OUTPUT):
	@printf '%s\n' '$(MESSAGE)' > $@

.PHONY: all clean
clean:
	@rm -f $(OUTPUT)

