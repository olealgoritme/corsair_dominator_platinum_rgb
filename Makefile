all:
	gcc -o corsair_dominator_platinum_rgb corsair_dominator_platinum_rgb.c -li2c
install:
	sudo cp corsair_dominator_platinum_rgb /usr/local/bin/
