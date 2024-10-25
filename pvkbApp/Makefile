TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================
PROD_LIBS += pvAccessCA ca pvAccess pvData Com

PROD_HOST += pvkb
pvkb_SRCS += pvkb.cpp
pvkb_LIBS += $(EPICS_BASE_HOST_LIBS)
pvkb_SYS_LIBS += ncurses

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
