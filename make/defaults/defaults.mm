# -*- Makefile -*-
#
# michael a.g. aïvázis <michael.aivazis@para-sim.com>
#
# (c) 2013-2018 parasim inc
# (c) 2010-2018 california institute of technology
# all rights reserved
#

# be quiet
.SILENT:
# shutdown make's implict rule database
.SUFFIXES:
# set the default goal so we don't depend on presentation order
.DEFAULT_GOAL = all

# initialize the various categories of variables
# tokens
include make/defaults/tokens.mm
# tools
include make/defaults/tools.mm
# help
include make/defaults/log.mm

# end of file
