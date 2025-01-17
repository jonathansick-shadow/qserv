CREATE USER IF NOT EXISTS 'qsmaster'@'localhost';

-- Used by xrootd Qserv plugin:
-- to publish LSST databases
DROP DATABASE IF EXISTS qservw_worker;
CREATE DATABASE qservw_worker;
GRANT SELECT ON qservw_worker.* TO 'qsmaster'@'localhost';
CREATE TABLE qservw_worker.Dbs (
  `db` char(200) NOT NULL
);

CREATE DATABASE IF NOT EXISTS qservScratch;
GRANT ALL ON qservScratch.* TO 'qsmaster'@'localhost';

GRANT SELECT ON mysql.* TO 'qsmaster'@'localhost';
GRANT ALL ON `q\_memoryLockDb`.* TO 'qsmaster'@'localhost';

-- Subchunks databases
GRANT ALL ON `Subchunks\_%`.* TO 'qsmaster'@'localhost';
