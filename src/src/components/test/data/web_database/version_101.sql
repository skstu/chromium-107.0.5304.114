PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE meta(key LONGVARCHAR NOT NULL UNIQUE PRIMARY KEY, value LONGVARCHAR);
INSERT INTO meta VALUES('mmap_status','-1');
INSERT INTO meta VALUES('version','101');
INSERT INTO meta VALUES('last_compatible_version','99');
INSERT INTO meta VALUES('Builtin Keyword Version','127');
CREATE TABLE token_service (service VARCHAR PRIMARY KEY NOT NULL,encrypted_token BLOB);
CREATE TABLE keywords (id INTEGER PRIMARY KEY,short_name VARCHAR NOT NULL,keyword VARCHAR NOT NULL,favicon_url VARCHAR NOT NULL,url VARCHAR NOT NULL,safe_for_autoreplace INTEGER,originating_url VARCHAR,date_created INTEGER DEFAULT 0,usage_count INTEGER DEFAULT 0,input_encodings VARCHAR,suggest_url VARCHAR,prepopulate_id INTEGER DEFAULT 0,created_by_policy INTEGER DEFAULT 0,last_modified INTEGER DEFAULT 0,sync_guid VARCHAR,alternate_urls VARCHAR,image_url VARCHAR,search_url_post_params VARCHAR,suggest_url_post_params VARCHAR,image_url_post_params VARCHAR,new_tab_url VARCHAR,last_visited INTEGER DEFAULT 0, created_from_play_api INTEGER DEFAULT 0, is_active INTEGER DEFAULT 0);
CREATE TABLE autofill (name VARCHAR, value VARCHAR, value_lower VARCHAR, date_created INTEGER DEFAULT 0, date_last_used INTEGER DEFAULT 0, count INTEGER DEFAULT 1, PRIMARY KEY (name, value));
CREATE TABLE credit_cards ( guid VARCHAR PRIMARY KEY, name_on_card VARCHAR, expiration_month INTEGER, expiration_year INTEGER, card_number_encrypted BLOB, date_modified INTEGER NOT NULL DEFAULT 0, origin VARCHAR DEFAULT '', use_count INTEGER NOT NULL DEFAULT 0, use_date INTEGER NOT NULL DEFAULT 0, billing_address_id VARCHAR, nickname VARCHAR);
CREATE TABLE autofill_profiles ( guid VARCHAR PRIMARY KEY, company_name VARCHAR, street_address VARCHAR, dependent_locality VARCHAR, city VARCHAR, state VARCHAR, zipcode VARCHAR, sorting_code VARCHAR, country_code VARCHAR, date_modified INTEGER NOT NULL DEFAULT 0, origin VARCHAR DEFAULT '', language_code VARCHAR, use_count INTEGER NOT NULL DEFAULT 0, use_date INTEGER NOT NULL DEFAULT 0, label VARCHAR, disallow_settings_visible_updates INTEGER NOT NULL DEFAULT 0);
CREATE TABLE autofill_profile_addresses ( guid VARCHAR, street_address VARCHAR, street_name VARCHAR, dependent_street_name VARCHAR, house_number VARCHAR, subpremise VARCHAR, premise_name VARCHAR, street_address_status INTEGER DEFAULT 0, street_name_status INTEGER DEFAULT 0, dependent_street_name_status INTEGER DEFAULT 0, house_number_status INTEGER DEFAULT 0, subpremise_status INTEGER DEFAULT 0, premise_name_status INTEGER DEFAULT 0, dependent_locality VARCHAR, city VARCHAR, state VARCHAR, zip_code VARCHAR, sorting_code VARCHAR, country_code VARCHAR, dependent_locality_status INTEGER DEFAULT 0, city_status INTEGER DEFAULT 0, state_status INTEGER DEFAULT 0, zip_code_status INTEGER DEFAULT 0, sorting_code_status INTEGER DEFAULT 0, country_code_status INTEGER DEFAULT 0, apartment_number VARCHAR, floor VARCHAR, apartment_number_status INTEGER DEFAULT 0, floor_status INTEGER DEFAULT 0);
CREATE TABLE autofill_profile_names ( guid VARCHAR, first_name VARCHAR, middle_name VARCHAR, last_name VARCHAR, full_name VARCHAR, honorific_prefix VARCHAR, first_last_name VARCHAR, conjunction_last_name VARCHAR, second_last_name VARCHAR, honorific_prefix_status INTEGER DEFAULT 0, first_name_status INTEGER DEFAULT 0, middle_name_status INTEGER DEFAULT 0, last_name_status INTEGER DEFAULT 0, first_last_name_status INTEGER DEFAULT 0, conjunction_last_name_status INTEGER DEFAULT 0, second_last_name_status INTEGER DEFAULT 0, full_name_status INTEGER DEFAULT 0, full_name_with_honorific_prefix VARCHAR, full_name_with_honorific_prefix_status INTEGER DEFAULT 0);
CREATE TABLE autofill_profile_emails ( guid VARCHAR, email VARCHAR);
CREATE TABLE autofill_profile_phones ( guid VARCHAR, number VARCHAR);
CREATE TABLE masked_credit_cards (id VARCHAR,name_on_card VARCHAR,network VARCHAR,last_four VARCHAR,exp_month INTEGER DEFAULT 0,exp_year INTEGER DEFAULT 0, bank_name VARCHAR, nickname VARCHAR, card_issuer INTEGER DEFAULT 0, instrument_id INTEGER DEFAULT 0, virtual_card_enrollment_state INTEGER DEFAULT 0, card_art_url VARCHAR);
CREATE TABLE unmasked_credit_cards (id VARCHAR,card_number_encrypted VARCHAR,unmask_date INTEGER NOT NULL DEFAULT 0);
CREATE TABLE server_card_metadata (id VARCHAR NOT NULL,use_count INTEGER NOT NULL DEFAULT 0, use_date INTEGER NOT NULL DEFAULT 0, billing_address_id VARCHAR);
CREATE TABLE server_addresses (id VARCHAR,company_name VARCHAR,street_address VARCHAR,address_1 VARCHAR,address_2 VARCHAR,address_3 VARCHAR,address_4 VARCHAR,postal_code VARCHAR,sorting_code VARCHAR,country_code VARCHAR,language_code VARCHAR, recipient_name VARCHAR, phone_number VARCHAR);
CREATE TABLE server_address_metadata (id VARCHAR NOT NULL,use_count INTEGER NOT NULL DEFAULT 0, use_date INTEGER NOT NULL DEFAULT 0, has_converted BOOL NOT NULL DEFAULT FALSE);
CREATE TABLE autofill_sync_metadata (model_type INTEGER NOT NULL, storage_key VARCHAR NOT NULL, value BLOB, PRIMARY KEY (model_type, storage_key));
CREATE TABLE autofill_model_type_state (model_type INTEGER NOT NULL PRIMARY KEY, value BLOB);
CREATE TABLE payments_customer_data (customer_id VARCHAR);
CREATE TABLE payments_upi_vpa (vpa VARCHAR);
CREATE TABLE server_card_cloud_token_data ( id VARCHAR, suffix VARCHAR, exp_month INTEGER DEFAULT 0, exp_year INTEGER DEFAULT 0, card_art_url VARCHAR, instrument_token VARCHAR);
CREATE TABLE offer_data ( offer_id UNSIGNED LONG, offer_reward_amount VARCHAR, expiry UNSIGNED LONG, offer_details_url VARCHAR, merchant_domain VARCHAR, promo_code VARCHAR, value_prop_text VARCHAR, see_details_text VARCHAR, usage_instructions_text VARCHAR);
CREATE TABLE offer_eligible_instrument ( offer_id UNSIGNED LONG,instrument_id UNSIGNED LONG);
CREATE TABLE offer_merchant_domain ( offer_id UNSIGNED LONG,merchant_domain VARCHAR);
CREATE INDEX autofill_name ON autofill (name);
CREATE INDEX autofill_name_value_lower ON autofill (name, value_lower);
COMMIT;
