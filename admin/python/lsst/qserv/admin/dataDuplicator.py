# LSST Data Management System
# Copyright 2014-2015 AURA/LSST.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.

"""
Script that runs data duplicator for integration tests.

Simply running the duplicator as listed in the documentation example here:
https://github.com/LSST/partition/blob/master/docs/duplication.md

@author  Vaikunth Thukral, TAMU/SLAC
"""

#--------------------------------
#  Imports of standard modules --
#--------------------------------
import logging
import os

#-----------------------------
# Imports for other modules --
#-----------------------------
from lsst.qserv.admin import commons

#----------------------------------
# Local non-exported definitions --
#----------------------------------

#------------------------
# Exported definitions --
#------------------------


class DataDuplicator(object):

    def __init__(self, data_reader, cfg_dir, out_dir):

        self.dataConfig = data_reader
        self.logger = logging.getLogger(__name__)

        self._cfgDirname = cfg_dir
        self._outDirname = out_dir
        self._tables = data_reader.duplicatedTables
        self._directorTable = data_reader.directors[0]

    def run(self):
        self._runIndex()
        self._runDuplicate()

    def _runIndex(self):
        """
        Run sph-htm-index as step 1 of the duplication process
        """

        for table in self._tables:
            if os.path.isfile(os.path.join(self._cfgDirname, table + '.cfg')) == False:
                self.logger.error("Path to indexing config file not found")

            self.logger.info("Running indexer with output for %r to %r" % (table, self._outDirname))
            run_index = commons.run_command(["sph-htm-index",
                                             "--config-file=" +
                                             os.path.join(self._cfgDirname, table + ".cfg"),
                                             "--config-file=" + os.path.join(self._cfgDirname, "common.cfg"),
                                             "--in=" + os.path.join(self._cfgDirname, table + ".txt"),
                                             "--out.dir=" + os.path.join(self._outDirname, "index/", table)])

    def _runDuplicate(self):
        """
        Run sph-duplicate to set up partitioned data that the data loader needs
        """

        for table in self._tables:
            if os.path.isfile(os.path.join(self._cfgDirname, 'common.cfg')) == False:
                self.logger.error("Path to duplicator config file not found")

            self.logger.info("Running duplicator for table %r" % table)
            run_dupl = commons.run_command(["sph-duplicate",
                                            "--config-file=" + os.path.join(self._cfgDirname, table + ".cfg"),
                                            "--config-file=" + os.path.join(self._cfgDirname, "common.cfg"),
                                            "--index=" + os.path.join(self._outDirname,
                                                                      "index/", table, "htm_index.bin"),
                                            "--part.index=" +
                                            os.path.join(self._outDirname, "index",
                                                         self._directorTable, "htm_index.bin"),
                                            "--out.dir=" + os.path.join(self._outDirname, "chunks/", table)])
