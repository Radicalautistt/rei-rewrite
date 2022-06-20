rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

#links = -ldl -lm -lxcb -lopenal -lcimgui -lpng -lstdc++

third_party_obj = third-party/build-outs/volk.o
third_party_obj += third-party/build-outs/vma_impl.o

include_paths = -isystem third-party
link_paths = -L third-party/build-outs
links = -lm -lstdc++ -lxcb -lpng -lcimgui -lopenal

flags = -std=c99 -Wall -Wpadded -Wextra -Wconversion -Og -march=native -fno-exceptions -ggdb
src = $(call rwildcard, src, *.c, *.h)
obj = $(patsubst src/%.c, obj/%.o, $(src)) $(third_party_obj)

rei: $(obj)
	gcc -o $@ $^ $(link_paths) $(links)

obj/%.o: src/%.c
	gcc $(flags) $(include_paths) -c -o $@ $<

run: rei
	./rei

clean:
	rm -r obj/
