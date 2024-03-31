-- components_unittests --gtest_filter=SharedStorageDatabaseTest.Version1_LoadFromFile
--
-- .dump of a version 1 Shared Storage database.
BEGIN TRANSACTION;
CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);
INSERT INTO "meta" VALUES('version','1');
INSERT INTO "meta" VALUES('last_compatible_version','1');
CREATE TABLE values_mapping(context_origin TEXT NOT NULL,key TEXT NOT NULL,value TEXT,PRIMARY KEY(context_origin,key)) WITHOUT ROWID;
INSERT INTO "values_mapping" VALUES ('http://google.com','key1','value1');
INSERT INTO "values_mapping" VALUES ('http://google.com','key2','value2');
INSERT INTO "values_mapping" VALUES ('http://youtube.com','visited','1111111');
INSERT INTO "values_mapping" VALUES ('http://chromium.org','a','');
INSERT INTO "values_mapping" VALUES ('http://chromium.org','b','hello');
INSERT INTO "values_mapping" VALUES ('http://chromium.org','c','goodbye');
INSERT INTO "values_mapping" VALUES ('http://gv.com','cookie','13268941793856733');
INSERT INTO "values_mapping" VALUES ('http://abc.xyz','seed','387562094');
INSERT INTO "values_mapping" VALUES ('http://abc.xyz','bucket','1276');
INSERT INTO "values_mapping" VALUES ('http://withgoogle.com','count','389');
INSERT INTO "values_mapping" VALUES ('http://waymo.com','key','value');
INSERT INTO "values_mapping" VALUES ('http://google.org','1','ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff');
INSERT INTO "values_mapping" VALUES ('http://google.org','2',';');
INSERT INTO "values_mapping" VALUES ('http://google.org','#','[]');
INSERT INTO "values_mapping" VALUES ('http://google.org','ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff','k');
INSERT INTO "values_mapping" VALUES ('http://growwithgoogle.com','_','down');
INSERT INTO "values_mapping" VALUES ('http://growwithgoogle.com','<','left');
INSERT INTO "values_mapping" VALUES ('http://growwithgoogle.com','>','right');
CREATE TABLE per_origin_mapping(context_origin TEXT NOT NULL PRIMARY KEY,last_used_time INTEGER NOT NULL,length INTEGER NOT NULL) WITHOUT ROWID;
INSERT INTO "per_origin_mapping" VALUES ('http://google.com',13266954476192362,2);
INSERT INTO "per_origin_mapping" VALUES ('http://youtube.com',13266954593856733,1);
INSERT INTO "per_origin_mapping" VALUES ('http://chromium.org',13268941676192362,3);
INSERT INTO "per_origin_mapping" VALUES ('http://gv.com',13268941793856733,1);
INSERT INTO "per_origin_mapping" VALUES ('http://abc.xyz',13269481776356965,2);
INSERT INTO "per_origin_mapping" VALUES ('http://withgoogle.com',13269545986263676,1);
INSERT INTO "per_origin_mapping" VALUES ('http://waymo.com',13269546064355176,1);
INSERT INTO "per_origin_mapping" VALUES ('http://google.org',13269546476192362,4);
INSERT INTO "per_origin_mapping" VALUES ('http://growwithgoogle.com',13269546593856733,3);
CREATE TABLE budget_mapping(id INTEGER NOT NULL PRIMARY KEY,context_origin TEXT NOT NULL,time_stamp INTEGER NOT NULL,bits_debit REAL NOT NULL);
INSERT INTO "budget_mapping" VALUES (1,'http://google.com',13266954476192332,1.0);
INSERT INTO "budget_mapping" VALUES (2,'http://google.com',13266954476192344,2.0);
INSERT INTO "budget_mapping" VALUES (3,'http://google.com',13266954476192362,1.1);
INSERT INTO "budget_mapping" VALUES (4,'http://youtube.com',13266954593856693,1.3);
INSERT INTO "budget_mapping" VALUES (5,'http://chromium.org',13268941676192362,1.5);
INSERT INTO "budget_mapping" VALUES (6,'http://youtube.com',13266954593856733,2.3);
INSERT INTO "budget_mapping" VALUES (7,'http://gv.com',13268941793856733,3.7);
INSERT INTO "budget_mapping" VALUES (8,'http://abc.xyz',13269481776356845,2.2);
INSERT INTO "budget_mapping" VALUES (9,'http://withgoogle.com',13269545986263676,1);
INSERT INTO "budget_mapping" VALUES (10,'http://abc.xyz',13269481776356905,1.1);
INSERT INTO "budget_mapping" VALUES (11,'http://waymo.com',13269546064355176,4.2);
INSERT INTO "budget_mapping" VALUES (12,'http://abc.xyz',13269481776356965,2.0);
INSERT INTO "budget_mapping" VALUES (13,'http://google.org',13269546476192362,4);
INSERT INTO "budget_mapping" VALUES (14,'http://growwithgoogle.com',13269546593856733,1.2);
CREATE INDEX IF NOT EXISTS per_origin_mapping_last_used_time_idx ON per_origin_mapping(last_used_time);
CREATE INDEX IF NOT EXISTS budget_mapping_origin_time_stamp_idx ON budget_mapping(context_origin,time_stamp);
COMMIT;
