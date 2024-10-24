TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================
PROD_LIBS += pvAccessCA ca pvAccess pvData Com

PROD_HOST += pvkb
main_SRCS += pvkb.cpp
main_LIBS += $(EPICS_BASE_HOST_LIBS)
main_SYS_LIBS += ncurses

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
