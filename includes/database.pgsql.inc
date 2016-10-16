<?php

/**
 * @file
 * Database interface code for PostgreSQL database servers.
 */

/**
 * @ingroup database
 * @{
 */

/**
 * Report database status.
 */
function db_status_report($phase) {
  $t = get_t();

  $version = db_version();

  $form['pgsql'] = array(
    'title' => $t('PostgreSQL database'),
    'value' => ($phase == 'runtime') ? l($version, 'admin/reports/status/sql') : $version,
  );

  if (version_compare($version, DRUPAL_MINIMUM_PGSQL) < 0) {
    $form['pgsql']['severity'] = REQUIREMENT_ERROR;
    $form['pgsql']['description'] = $t('Your PostgreSQL Server is too old. Drupal requires at least PostgreSQL %version.', array('%version' => DRUPAL_MINIMUM_PGSQL));
  }

  return $form;
}

/**
 * Returns the version of the database server currently in use.
 *
 * @return string
 *   Database server version
 */
function db_version() {
  return db_result(db_query("SHOW SERVER_VERSION"));
}

/**
 * Initialize a database connection.
 *
 * @param string $url
 *   The database connection URL.
 * @return resource
 *   The database connection.
 */
function db_connect($url) {
  global $db_collation;

  // Check if PostgreSQL support is present in PHP
  if (!function_exists('pg_connect')) {
    _db_error_page('Unable to use the PostgreSQL database because the PostgreSQL extension for PHP is not installed. Check your <code>php.ini</code> to see how you can enable it.');
  }

  $url_parts = parse_url($url);
  $conn_string = '';

  // Decode urlencoded information in the database connection URL
  if (isset($url_parts['host'])) {
    $conn_string .= ' host='. urldecode($url_parts['host']);
  }
  if (isset($url_parts['port'])) {
    $conn_string .= ' port='. $url_parts['port'];
  }
  if (isset($url_parts['path'])) {
    $conn_string .= ' dbname='. substr(urldecode($url_parts['path']), 1);
  }
  if (isset($url_parts['user'])) {
    $conn_string .= ' user='. urldecode($url_parts['user']);
  }
  if (isset($url_parts['pass'])) {
    $conn_string .= ' password='. urldecode($url_parts['pass']);
  }

  // pg_last_error() does not return a useful error message for database
  // connection errors. We must turn on error tracking to get at a good error
  // message, which will be stored in $php_errormsg.
  $track_errors = ini_get('track_errors');
  ini_set('track_errors', 1);

  $db_conn = @pg_connect($conn_string);
  if (!$db_conn) {
    require_once './includes/unicode.inc';
    _db_error_page(decode_entities($php_errormsg));
  }

  // Restore error tracking setting
  ini_set('track_errors', $track_errors);

  pg_query($db_conn, "SET CLIENT_ENCODING TO 'UTF8'");
  return $db_conn;
}

/**
 * Helper function for db_query().
 */
function _db_query($query, $debug = 0) {
  global $active_db, $last_result, $queries;

  if (variable_get('dev_query', 0)) {
    list($usec, $sec) = explode(' ', microtime());
    $timer = (float)$usec + (float)$sec;
  }

  $last_result = pg_query($active_db, $query);

  if (variable_get('dev_query', 0)) {
    $bt = debug_backtrace();
    $query = $bt[2]['function'] ."\n". $query;
    list($usec, $sec) = explode(' ', microtime());
    $stop = (float)$usec + (float)$sec;
    $diff = $stop - $timer;
    $queries[] = array($query, $diff);
  }

  if ($debug) {
    print '<p>query: '. $query .'<br />error:'. pg_last_error($active_db) .'</p>';
  }

  if ($last_result !== FALSE) {
    return $last_result;
  }
  else {
    // Indicate to drupal_error_handler that this is a database error.
    ${DB_ERROR} = TRUE;
    trigger_error(check_plain(pg_last_error($active_db) ."\nquery: ". $query), E_USER_WARNING);
    return FALSE;
  }
}

/**
 * Fetch one result row from the previous query as an object.
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 * @return object|bool
 *   An object representing the next row of the result, or FALSE. The attributes
 *   of this object are the table fields selected by the query.
 */
function db_fetch_object($result) {
  return pg_fetch_object($result);
}

/**
 * Fetch one result row from the previous query as an array.
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 * @return array|bool
 *   An associative array representing the next row of the result, or FALSE.
 *   The keys of this object are the names of the table fields selected by the
 *   query, and the values are the field values for this result row.
 */
function db_fetch_array($result) {
  return pg_fetch_assoc($result);
}

/**
 * Return an individual result field from the previous query.
 *
 * Only use this function if exactly one field is being selected; otherwise,
 * use db_fetch_object() or db_fetch_array().
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 * @return mixed
 *   The resulting field or FALSE.
 */
function db_result($result) {
  if (pg_num_rows($result) > 0) {
    $array = pg_fetch_row($result);
    return $array[0];
  }
  return FALSE;
}

/**
 * Determine whether the previous query caused an error.
 *
 * @return int
 *   The error number from the last query, or 0 (zero) if no error occurred.
 */
function db_error() {
  global $active_db;
  return pg_last_error($active_db);
}

/**
 * Returns the last insert id. This function is thread safe.
 *
 * @param string $table
 *   The name of the table you inserted into.
 * @param string $field
 *   The name of the autoincrement field.
 * @return int
 *   The auto generated id used in the last query.
 */
function db_last_insert_id($table, $field) {
  return db_result(db_query("SELECT currval('{%l}_%l_seq')", $table, $field));
}

/**
 * Determine the number of rows changed by the preceding query.
 *
 * @return int
 *   The number of affected rows on success, and -1 if the last query failed.
 */
function db_affected_rows() {
  global $last_result;
  return empty($last_result) ? -1 : pg_affected_rows($last_result);
}

/**
 * Runs a limited-range query in the active database.
 *
 * Use this as a substitute for db_query() when a subset of the query is to be
 * returned.
 * User-supplied arguments to the query should be passed in as separate
 * parameters so that they can be properly escaped to avoid SQL injection
 * attacks.
 *
 * @param string $query
 *   A string containing an SQL query.
 * @param ...
 *   A variable number of arguments which are substituted into the query
 *   using printf() syntax. Instead of a variable number of query arguments,
 *   you may also pass a single array containing the query arguments.
 *   Valid %-modifiers are: %s, %d, %f, %b (binary data, do not enclose
 *   in '') and %%.
 *
 *   NOTE: using this syntax will cast FALSE values to decimal 0, and TRUE
 *   values to decimal 1.
 * @param int $from
 *   The first result row to return.
 * @param int $count
 *   The maximum number of result rows to return.
 * @return resource|bool
 *   A database query result resource, or FALSE if the query was not executed
 *   correctly.
 */
function db_query_range() {
  $args = func_get_args();
  $query = array_shift($args);
  $count = array_pop($args);
  $from = array_pop($args);

  if (isset($args[0]) && is_array($args[0])) { // 'All arguments in one array' syntax
    $args = $args[0];
  }

  $query .= _db_process_sql(' LIMIT %d OFFSET %d', $count, $from);
  return db_query($query, $args);
}

/**
 * Runs a SELECT query and stores its results in a temporary table.
 *
 * Use this as a substitute for db_query() when the results need to be stored
 * in a temporary table.
 *
 * User-supplied arguments to the query should be passed in as separate parameters
 * so that they can be properly escaped to avoid SQL injection attacks.
 *
 * Note that if you need to know how many results were returned, you should do
 * a SELECT COUNT(*) on the temporary table afterwards. db_affected_rows() does
 * not give consistent result across different database types in this case.
 *
 * @param string $query
 *   A string containing a normal SELECT SQL query.
 * @param ...
 *   A variable number of arguments which are substituted into the query
 *   using printf() syntax. The query arguments can be enclosed in one
 *   array instead.
 *   Valid %-modifiers are: %s, %d, %f, %b (binary data, do not enclose
 *   in '') and %%.
 *
 *   NOTE: using this syntax will cast FALSE values to decimal 0, and TRUE
 *   values to decimal 1.
 * @param string $table
 *   The name of the temporary table to select into. This name will not be
 *   prefixed as there is no risk of collision.
 * @return resource|bool
 *   A database query result resource, or FALSE if the query was not executed
 *   correctly.
 */
function db_query_temporary() {
  $args = func_get_args();
  $query = array_shift($args);
  $table = array_pop($args);

  if (isset($args[0]) && is_array($args[0])) { // 'All arguments in one array' syntax
    $args = $args[0];
  }

  // Table name will not be prefixed as there is no risk of collision.
  $query = preg_replace('/^\s*SELECT\b/i', 'CREATE TEMPORARY TABLE %l AS SELECT', $query);
  array_unshift($args, $table);
  return db_query($query, $args);
}

/**
 * Returns a properly formatted Binary Large Object value.
 * In case of PostgreSQL encodes data for insert into bytea field.
 *
 * @param string $data
 *   Data to encode.
 * @return string
 *   Encoded data.
 */
function db_encode_blob($data) {
  return "'". pg_escape_bytea($data) ."'";
}

/**
 * Returns text from a Binary Large Object value.
 * In case of PostgreSQL decodes data after select from bytea field.
 *
 * @param string $data
 *   Data to decode.
 * @return string
 *   Decoded data.
 */
function db_decode_blob($data) {
  return pg_unescape_bytea($data);
}

/**
 * Prepare user input for use in a database query, preventing SQL injection attacks.
 * Note: This function requires PostgreSQL 7.2 or later.
 *
 * @param string $text
 *   The string to escape.
 * @return string
 *   The escaped string.
 */
function db_escape_string($text) {
  return pg_escape_string($text);
}

/**
 * Lock a table.
 * This function automatically starts a transaction.
 *
 * @param string $table
 *   The name of the table to lock.
 */
function db_lock_table($table) {
  db_query('BEGIN; LOCK TABLE {%l} IN EXCLUSIVE MODE', $table);
}

/**
 * Unlock all locked tables.
 * This function automatically commits a transaction.
 */
function db_unlock_tables() {
  db_query('COMMIT');
}

/**
 * Check if a table exists.
 *
 * @param string $table
 *   The name of the table.
 * @return bool
 *   TRUE if the table exists, and FALSE if the table does not exist.
 */
function db_table_exists($table) {
  return (bool) db_result(db_query("SELECT COUNT(*) FROM pg_class WHERE relname LIKE '{%l}'", $table));
}

/**
 * Check if a column exists in the given table.
 *
 * @param string $table
 *   The name of the table.
 * @param string $field
 *   The name of the column.
 * @return bool
 *   TRUE if the column exists, and FALSE if the column does not exist.
 */
function db_column_exists($table, $field) {
  return (bool) db_result(db_query("SELECT COUNT(*) FROM pg_class c, pg_attribute a WHERE a.attrelid = c.oid AND c.relname LIKE '{%l}' AND a.attname LIKE '%l'", $table, $field));
}

/**
 * Verify if the database is set up correctly.
 */
function db_check_setup() {
  $t = get_t();
  $encoding = db_result(db_query('SHOW SERVER_ENCODING'));

  if (!in_array(strtolower($encoding), array('unicode', 'utf8'))) {
    drupal_set_message($t('Your PostgreSQL database is set up with the wrong character encoding (%encoding). It is possible it will not work as expected. It is advised to recreate it with UTF-8/Unicode encoding. More information can be found in the <a href="@url">PostgreSQL documentation</a>.', array('%encoding' => $encoding, '@url' => 'http://www.postgresql.org/docs/7.4/interactive/multibyte.html')), 'status');
  }
}

/**
 * @} End of "ingroup database".
 */

/**
 * @ingroup schemaapi
 * @{
 */

/**
 * This maps a generic data type in combination with its data size
 * to the engine-specific data type.
 */
function db_type_map() {
  static $map;

  if (!isset($map)) {
    // Put :normal last so it gets preserved by array_flip.  This makes
    // it much easier for modules (such as schema.module) to map
    // database types back into schema types.
    $map = array(
      'int:tiny' => 'smallint',
      'int:small' => 'smallint',
      'int:medium' => 'integer',
      'int:big' => 'bigint',
      'int:normal' => 'integer',

      'float:tiny' => 'real',
      'float:small' => 'real',
      'float:medium' => 'real',
      'float:big' => 'double precision',
      'float:normal' => 'real',

      'numeric:normal' => 'numeric',

      'serial:tiny' => 'smallserial',
      'serial:small' => 'smallserial',
      'serial:medium' => 'serial',
      'serial:big' => 'bigserial',
      'serial:normal' => 'serial',

      'varchar:normal' => 'character varying',
      'char:normal' => 'character',

      'text:tiny' => 'text',
      'text:small' => 'text',
      'text:medium' => 'text',
      'text:big' => 'text',
      'text:normal' => 'text',

      'blob:big' => 'bytea',
      'blob:normal' => 'bytea',

      'datetime:normal' => 'timestamp without time zone',
    );
  }

  return $map;
}

/**
 * Generate SQL to create a new table from a Drupal schema definition.
 *
 * @param string $table
 *   The name of the table to create.
 * @param array $schema
 *   A Schema API table definition array.
 * @return array
 *   An array of SQL statements to create the table.
 */
function db_create_table_sql($table, $schema) {
  $statements = array();
  $table = db_escape_table($table);
  $sql = 'CREATE TABLE {'. $table .'} (';

  // Add the SQL statement for each field.
  $fields = array();
  foreach ($schema['fields'] as $field => $spec) {
    $fields[] = _db_create_field_sql($field, $spec);
  }
  $sql .= implode(', ', $fields);

  // Process keys.
  $keys = array();

  if (!empty($schema['primary key'])) {
    $keys[] = 'PRIMARY KEY ('. _db_create_key_sql($schema['primary key']) .')';
  }

  if (!empty($schema['unique keys'])) {
    foreach ($schema['unique keys'] as $key => $fields) {
      $keys[] = 'CONSTRAINT {'. $table .'}_'. db_escape_table($key) .'_key UNIQUE ('. _db_create_key_sql($fields) .')';
    }
  }

  if (!empty($keys)) {
    $sql .= ', '. implode(', ', $keys);
  }

  $sql .= ')';
  $statements[] = $sql;

  if (!empty($schema['indexes'])) {
    foreach ($schema['indexes'] as $index => $fields) {
      $statements[] = _db_create_index_sql($table, $index, $fields);
    }
  }

  return $statements;
}

function _db_create_index_sql($table, $index, $fields) {
  $table = db_escape_name($table);
  $index = db_escape_name($index);
  return 'CREATE INDEX {' . $table . '}_' . $index . '_idx ON {' . $table . '} (' . _db_create_key_sql($fields) .')';
}

function _db_create_key_sql($fields) {
  $ret = array();

  foreach ($fields as $field) {
    if (is_array($field)) {
      $ret[] = 'substr('. db_escape_table($field[0]) .', 1, '. (int) $field[1] .')';
    }
    else {
      $ret[] = db_escape_table($field);
    }
  }

  return implode(', ', $ret);
}

function _db_create_keys(&$ret, $table, $schema) {
  if (!empty($schema['primary key'])) {
    db_add_primary_key($ret, $table, $schema['primary key']);
  }

  if (!empty($schema['unique keys'])) {
    foreach ($schema['unique keys'] as $key => $fields) {
      db_add_unique_key($ret, $table, $key, $fields);
    }
  }

  if (!empty($schema['indexes'])) {
    foreach ($schema['indexes'] as $index => $fields) {
      db_add_index($ret, $table, $index, $fields);
    }
  }
}

/**
 * Create an SQL string for a field to be used in table creation or alteration.
 *
 * @param string $field
 *   Name of the field.
 * @param array $spec
 *   The field specification, as per the schema data structure format.
 * @return string
 *   The SQL string for the field.
 */
function _db_create_field_sql($field, $spec) {
  $field = db_escape_table($field);

  if (!isset($spec['size'])) {
    $spec['size'] = 'normal';
  }

  if ($spec['type'] == 'serial') {
    $spec['unsigned'] = FALSE;
    $spec['not null'] = FALSE;
    unset($spec['default']);
  }

  // Set the correct database-engine specific data type.
  if (!isset($spec['pgsql type'])) {
    $map = db_type_map();
    $spec['pgsql type'] = $map[$spec['type'] .':'. $spec['size']];
  }

  $sql = $field .' '. $spec['pgsql type'];

  if (in_array($spec['type'], array('varchar', 'char', 'text')) && isset($spec['length'])) {
    $sql .= '('. (int) $spec['length'] .')';
  }
  elseif ($spec['type'] == 'numeric' && isset($spec['precision']) && isset($spec['scale'])) {
    $sql .= '('. (int) $spec['precision'] .', '. (int) $spec['scale'] .')';
  }

  if (!empty($spec['unsigned'])) {
    $sql .= " CHECK ($field >= 0)";
  }

  if (!empty($spec['not null'])) {
    $sql .= ' NOT NULL';
  }

  if (isset($spec['default'])) {
    $sql .= _db_process_sql(' DEFAULT '. db_type_placeholder($spec['type']), $spec['default']);
  }

  return $sql;
}

/**
 * Rename a table.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be renamed.
 * @param string $new_table
 *   The new name for the table.
 */
function db_rename_table(&$ret, $table, $new_table) {
  $ret[] = update_sql('ALTER TABLE {%l} RENAME TO {%l}', $table, $new_table);
}

/**
 * Drop a table.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be dropped.
 */
function db_drop_table(&$ret, $table) {
  $ret[] = update_sql('DROP TABLE {%l}', $table);
}

/**
 * Add a new field to a table.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   Name of the table to be altered.
 * @param string $field
 *   Name of the field to be added.
 * @param array $spec
 *   The field specification array, as taken from a schema definition.
 *   The specification may also contain the key 'initial', the newly
 *   created field will be set to the value of the key in all rows.
 *   This is most useful for creating NOT NULL columns with no default
 *   value in existing tables.
 * @param array $new_keys (optional)
 *   Keys and indexes specification to be created on the table along with
 *   adding the field. The format is the same as a table specification but
 *   without the 'fields' element.  If you are adding a type 'serial' field,
 *   you MUST specify at least one key or index including it in this array.
 * @see db_change_field()
 */
function db_add_field(&$ret, $table, $field, $spec, $new_keys = array()) {
  $fix_null = FALSE;
  if (!empty($spec['not null']) && !isset($spec['default'])) {
    $fix_null = TRUE;
    $spec['not null'] = FALSE;
  }

  $query = 'ALTER TABLE {%l} ADD COLUMN '. _db_create_field_sql($field, $spec);
  $ret[] = update_sql($query, $table);

  if (isset($spec['initial'])) {
    $ret[] = update_sql('UPDATE {%l} SET %l = '. db_type_placeholder($spec['type']), $table, $field, $spec['initial']);
  }

  if ($fix_null) {
    $ret[] = update_sql('ALTER TABLE {%l} ALTER $field SET NOT NULL', $table);
  }

  if (!empty($new_keys)) {
    _db_create_keys($ret, $table, $new_keys);
  }
}

/**
 * Drop a field.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $field
 *   The field to be dropped.
 */
function db_drop_field(&$ret, $table, $field) {
  $ret[] = update_sql('ALTER TABLE {%l} DROP COLUMN %l', $table, $field);
}

/**
 * Set the default value for a field.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $field
 *   The field to be altered.
 * @param mixed $default
 *   Default value to be set. NULL for 'DEFAULT NULL'.
 */
function db_field_set_default(&$ret, $table, $field, $default) {
  $ret[] = update_sql('ALTER TABLE {%l} ALTER COLUMN %l SET DEFAULT %v', $table, $field, $default);
}

/**
 * Set a field to have no default value.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $field
 *   The field to be altered.
 */
function db_field_set_no_default(&$ret, $table, $field) {
  $ret[] = update_sql('ALTER TABLE {%l} ALTER COLUMN %l DROP DEFAULT', $table, $field);
}

/**
 * Add a primary key.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param array $fields
 *   Fields for the primary key.
 */
function db_add_primary_key(&$ret, $table, $fields) {
  $ret[] = update_sql('ALTER TABLE {%l} ADD PRIMARY KEY ('. _db_create_key_sql($fields) .')', $table);
}

/**
 * Drop the primary key.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 */
function db_drop_primary_key(&$ret, $table) {
  $ret[] = update_sql('ALTER TABLE {%l} DROP CONSTRAINT {%l}_pkey', $table, $table);
}

/**
 * Add a unique key.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $key
 *   The name of the key.
 * @param array $fields
 *   An array of field names.
 */
function db_add_unique_key(&$ret, $table, $key, $fields) {
  $ret[] = update_sql('ALTER TABLE {%l} ADD CONSTRAINT {%l}_%l_key UNIQUE ('. _db_create_key_sql($fields) .')', $table, $table, $key);
}

/**
 * Drop a unique key.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $key
 *   The name of the key.
 */
function db_drop_unique_key(&$ret, $table, $key) {
  $ret[] = update_sql('ALTER TABLE {%l} DROP CONSTRAINT {%l}_%l_key', $table, $table, $key);
}

/**
 * Add an index.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $index
 *   The name of the index.
 * @param array $fields
 *   An array of field names.
 */
function db_add_index(&$ret, $table, $index, $fields) {
  $ret[] = update_sql(_db_create_index_sql($table, $index, $fields));
}

/**
 * Drop an index.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   The table to be altered.
 * @param string $index
 *   The name of the index.
 */
function db_drop_index(&$ret, $table, $index) {
  $ret[] = update_sql('DROP INDEX {%l}_%l_idx', $table, $index);
}

/**
 * Change a field definition.
 *
 * IMPORTANT NOTE: To maintain database portability, you have to explicitly
 * recreate all indices and primary keys that are using the changed field.
 *
 * That means that you have to drop all affected keys and indexes with
 * db_drop_{primary_key,unique_key,index}() before calling db_change_field().
 * To recreate the keys and indices, pass the key definitions as the
 * optional $new_keys argument directly to db_change_field().
 *
 * For example, suppose you have:
 * @code
 * $schema['foo'] = array(
 *   'fields' => array(
 *     'bar' => array('type' => 'int', 'not null' => TRUE)
 *   ),
 *   'primary key' => array('bar')
 * );
 * @endcode
 * and you want to change foo.bar to be type serial, leaving it as the
 * primary key.  The correct sequence is:
 * @code
 * db_drop_primary_key($ret, 'foo');
 * db_change_field($ret, 'foo', 'bar', 'bar',
 *   array('type' => 'serial', 'not null' => TRUE),
 *   array('primary key' => array('bar')));
 * @endcode
 *
 * The reasons for this are due to the different database engines:
 *
 * On PostgreSQL, changing a field definition involves adding a new field
 * and dropping an old one which* causes any indices, primary keys and
 * sequences (from serial-type fields) that use the changed field to be dropped.
 *
 * On MySQL, all type 'serial' fields must be part of at least one key
 * or index as soon as they are created.  You cannot use
 * db_add_{primary_key,unique_key,index}() for this purpose because
 * the ALTER TABLE command will fail to add the column without a key
 * or index specification.  The solution is to use the optional
 * $new_keys argument to create the key or index at the same time as
 * field.
 *
 * You could use db_add_{primary_key,unique_key,index}() in all cases
 * unless you are converting a field to be type serial. You can use
 * the $new_keys argument in all cases.
 *
 * @param array $ret
 *   Array to which query results will be added.
 * @param string $table
 *   Name of the table.
 * @param string $field
 *   Name of the field to change.
 * @param string $new_field
 *   New name for the field (set to the same as $field if you don't want to
 *   change the name).
 * @param array $spec
 *   The field specification for the new field.
 * @param array $new_keys (optional)
 *   Keys and indexes specification to be created on the table along with
 *   changing the field. The format is the same as a table specification but
 *   without the 'fields' element.
 */
function db_change_field(&$ret, $table, $field, $new_field, $spec, $new_keys = array()) {
  $ret[] = update_sql('ALTER TABLE {%l} RENAME COLUMN %l TO %l_old', $table, $field, $field);
  $not_null = !empty($spec['not null']);
  $spec['not null'] = FALSE;
  db_add_field($ret, $table, $new_field, $spec);

  // We need to type cast the new column to best transfer the data
  // db_type_map will return possibilities that are not 'cast-able'
  // such as serial - they must be made 'int' instead.
  $map =  db_type_map();
  $type = $map[$spec['type'] .':'. $spec['size']];
  if (in_array($type, array('smallserial', 'serial', 'bigserial', 'numeric'))) {
    $type = 'integer';
  }

  $ret[] = update_sql("UPDATE {%l} SET %l = CAST(%l_old AS '%s')", $table, $new_field, $field, $type);

  if ($not_null) {
    $ret[] = update_sql('ALTER TABLE {%l} ALTER COLUMN %l SET NOT NULL', $table, $new_field);
  }

  db_drop_field($ret, $table, $field .'_old');

  if (!empty($new_keys)) {
    _db_create_keys($ret, $table, $new_keys);
  }
}

/**
 * @} End of "ingroup schemaapi".
 */
