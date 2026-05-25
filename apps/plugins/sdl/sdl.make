#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
# $Id$
#

SDL_SRCDIR := $(APPSDIR)/plugins/sdl
SDL_OBJDIR := $(BUILDDIR)/apps/plugins/sdl

SDL_SRC := $(call preprocess, $(SDL_SRCDIR)/SOURCES)

SDL_OBJ := $(call c2obj, $(SDL_SRC))

# add source files to OTHER_SRC to get automatic dependencies
OTHER_SRC += $(SDL_SRC)
OTHER_INC += -I$(SDL_SRCDIR)/include

# include comes first because of possible system SDL headers taking
# precedence
# some of these are for Quake only
SDLFLAGS = -I$(SDL_SRCDIR)/include $(filter-out -O%,$(PLUGINFLAGS))		\
-O3 -Wno-unused-parameter -Xpreprocessor -Wno-undef -Wcast-align	\
-ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations	\
-D_GNU_SOURCE=1 -D_REENTRANT -DSDL -DELF -w # disable all warnings \
-fpermissive

# WIP SDLFLAGS for warning deletions
#SDLFLAGS = -I$(SDL_SRCDIR)/include $(filter-out -O%,$(PLUGINFLAGS))		\
#-O3 -Wno-unused-parameter -Xpreprocessor -Wno-undef -Wno-sign-compare \
#-Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unknown-pragmas \
#-ffast-math -funroll-loops -fomit-frame-pointer -fexpensive-optimizations	\
#-D_GNU_SOURCE=1 -D_REENTRANT -DSDL -DELF

# CTRU does need these to avoid compiler errors
ifeq ($(APP_TYPE),ctru-app)
SDLFLAGS += -Wno-int-conversion -Wno-incompatible-pointer-types        \
-Wno-implicit-function-declaration -Wno-implicit-int
endif

# SDL games (duke3d/wolf3d/quake) disabled - GCC 14 incompatibility

###

# common

$(SDL_OBJDIR)/%.o: $(SDL_SRCDIR)/%.c $(SDL_SRCDIR)/sdl.make $(BUILDDIR)/sysfont.h
	$(SILENT)mkdir -p $(dir $@)
	$(call PRINTS,CC $(call full_path_subst,$(ROOTDIR)/%,%,$<))$(CC) -I$(dir $<) $(SDLFLAGS) -c $< -o $@
	$(SILENT)$(OC) --redefine-syms=$(SDL_SRCDIR)/redefines.txt $@
