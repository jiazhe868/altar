# -*- python -*-
# -*- coding: utf-8 -*-
#
# michael a.g. aïvázis <michael.aivazis@para-sim.com>
#
# (c) 2013-2018 parasim inc
# (c) 2010-2018 california institute of technology
# all rights reserved
#

# get the package
import altar

# get the protocol
from . import distribution
# and my base class
from .Base import Base as base


# the declaration
class Uniform(base, family="altar.distributions.uniform"):
    """
    The uniform probability distribution
    """


    # user configurable state
    support = altar.properties.array(default=(0,1))
    support.doc = "the support interval of the prior distribution"


    # protocol obligations
    @altar.provides
    def initialize(self, rng):
        """
        Initialize with the given runtime {context}
        """
        # set up my pdf
        self.pdf = altar.pdf.uniform(rng=rng.rng, support=self.support)
        # all done
        return self


# end of file
