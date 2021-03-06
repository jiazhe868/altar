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


# the protocol
class Distribution(altar.protocol, family="altar.distributions"):
    """
    The protocol that all AlTar probability distributions must satisfy
    """


    # requirements
    @altar.provides
    def initialize(self, **kwds):
        """
        Initialize with the given runtime {context}
        """


    @altar.provides
    def sample(self):
        """
        Sample the distribution using a random number generator
        """


    @altar.provides
    def density(self, x):
        """
        Compute the probability density of the distribution at {x}
        """


    @altar.provides
    def vector(self, vector):
        """
        Fill {vector} with random values
        """


    @altar.provides
    def matrix(self, matrix):
        """
        Fill {matrix} with random values
        """


    # framework hooks
    @classmethod
    def pyre_default(cls):
        """
        Supply a default implementation
        """
        # use the uniform distribution
        from .Uniform import Uniform as default
        # and return it
        return default


# end of file
