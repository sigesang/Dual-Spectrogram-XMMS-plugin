# Change paths & OPT if necessary

CC = gcc
OPT = -m486 -O2
#OPT = -mpentium -O6
#OPT = -mcpu=k6 -march=k6 -O6
CFLAGS = $(OPT) -Wall -fPIC `gtk-config --cflags gthread`
LFLAGS = -shared -fPIC -L/usr/local/lib
NAME = dspectogram
OBJ = $(NAME).o
INSTALL-DIR=`xmms-config --visualization-plugin-dir`
XMMS_DATADIR=`xmms-config --data-dir`
#INSTALL-DIR=$(HOME)/.xmms/Plugins
#XMMS_DATADIR=$(HOME)/.xmms
THEME_SUBDIR=$(NAME)_themes
XMMS_DATADIR_FLAGS=-DTHEMEDIR=\"$(XMMS_DATADIR)/$(THEME_SUBDIR)/\"
VER=`(grep 'define.*THIS_IS' $(NAME).c | tr -d [:alpha:][:blank:]\"\#_[=\n=] )`

all: lib$(NAME).so

lib$(NAME).so: $(OBJ) 
	$(CC) -o lib$(NAME).so $(OBJ) $(LFLAGS)

.c.o:
	$(CC) $(CFLAGS) $(XMMS_DATADIR_FLAGS) -c $< 

clean:
	rm -f *.o core *.so* 

distclean:
	rm -f *.o core *~

install:
	install lib$(NAME).so $(INSTALL-DIR) 
	mkdir -p $(XMMS_DATADIR)/$(THEME_SUBDIR)
	install bg_*.xpm $(XMMS_DATADIR)/$(THEME_SUBDIR)

release: lib$(NAME).so
	strip lib$(NAME).so
	@echo Creating $(NAME)_v$(VER).tar.gz
	mkdir -p $(NAME)-v$(VER)
	cp Makefile COPYING Changes README UPGRADE bg*.xpm $(NAME).c $(NAME)_mini.xpm lib$(NAME).so $(NAME)-v$(VER)
	tar cvzf $(NAME)-v$(VER).tar.gz $(NAME)-v$(VER)
	rm -rf $(NAME)-v$(VER)
