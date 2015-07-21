all: stpd

utils.o:utils.c
	gcc -c -Wall utils.c

stpd.o:stpd.c
	gcc -c -Wall stpd.c

stpd: stpd.o utils.o
	gcc -o stpd stpd.o utils.o -lnsl -lpthread
