<?php

/**
 * @file
 * Functions shared between mysql and mysqli database engines.
 */

/**
 * Report database status.
 */
function db_status_report($phase) {
  $t = get_t();

  $version = db_version();

  $form['mysql'] = array(
    'title' => $t('MySQL database'),
    'value' => ($phase == 'runtime') ? l($version, 'admin/reports/status/sql') : $version,
  );

  if (version_compare($version, DRUPAL_MINIMUM_MYSQL) < 0) {
    $form['mysql']['severity'] = REQUIREMENT_ERROR;
    $form['mysql']['description'] = $t('Your MySQL Server is too old. Drupal requires at least MySQL %version.', array('%version' => DRUPAL_MINIMUM_MYSQL));
  }

  return $form;
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

  $query .= _db_process_sql(' LIMIT %d, %d', $from, $count);
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
  $query = preg_replace('/^\s*SELECT\b/i', 'CREATE TEMPORARY TABLE %l ENGINE = HEAP SELECT', $query);
  array_unshift($args, $table);
  return db_query($query, $args);
}

/**
 * @ingroup schemaapi
 * @{
 */

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
  global $db_collation;

  $table = db_escape_table($table);
  $sql = 'CREATE TABLE {'. $table .'} (';

  // Add the SQL statement for each field.
  $fields = array();
  foreach ($schema['fields'] as $field => $spec) {
    $fields[] = _db_create_field_sql($field, $spec);
  }
  $sql .= implode(', ', $fields);

  // Process keys & indexes.
  $keys = _db_create_keys_sql($schema);
  if (!empty($keys)) {
    $sql .= ', '. implode(', ', $keys) ;
  }

  $sql .= ') ';

  if (empty($schema['mysql_suffix'])) {
    $schema['mysql_suffix'] = '/*!40100 DEFAULT CHARACTER SET utf8';
    // By default, MySQL uses the default collation for new tables, which is
    // 'utf8_general_ci' for utf8. If an alternate collation has been set, it
    // needs to be explicitly specified.
    // @see db_connect()
    $collation = !empty($schema['collation']) ? $schema['collation'] : (!empty($db_collation) ? $db_collation : NULL);
    if (!empty($collation)) {
      $schema['mysql_suffix'] .= ' COLLATE ' . $collation;
    }
    $schema['mysql_suffix'] .= ' */';
  }

  $sql .= $schema['mysql_suffix'];

  return array($sql);
}

function _db_create_keys_sql($schema) {
  $keys = array();

  if (!empty($schema['primary key'])) {
    $keys[] = 'PRIMARY KEY ('. _db_create_key_sql($schema['primary key']) .')';
  }

  if (!empty($schema['unique keys'])) {
    foreach ($schema['unique keys'] as $key => $fields) {
      $keys[] = 'UNIQUE KEY '. $key .' ('. _db_create_key_sql($fields) .')';
    }
  }

  if (!empty($schema['indexes'])) {
    foreach ($schema['indexes'] as $index => $fields) {
      $keys[] = 'INDEX '. $index .' ('. _db_create_key_sql($fields) .')';
    }
  }

  return $keys;
}

function _db_create_key_sql($fields) {
  $ret = array();

  foreach ($fields as $field) {
    if (is_array($field)) {
      $ret[] = db_escape_table($field[0]) .'('. (int) $field[1] .')';
    }
    else {
      $ret[] = db_escape_table($field);
    }
  }

  return implode(', ', $ret);
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
    $spec['unsigned'] = TRUE;
    $spec['not null'] = TRUE;
    unset($spec['default']);
  }

  // Set the correct database-engine specific data type.
  if (!isset($spec['mysql type'])) {
    $map = db_type_map();
    $spec['mysql type'] = $map[$spec['type'] .':'. $spec['size']];
  }

  $sql = $field .' '. $spec['mysql type'];

  if (in_array($spec['type'], array('varchar', 'char', 'text')) && isset($spec['length'])) {
    $sql .= '('. (int) $spec['length'] .')';
  }
  elseif ($spec['type'] == 'numeric' && isset($spec['precision']) && isset($spec['scale'])) {
    $sql .= '('. (int) $spec['precision'] .', '. (int) $spec['scale'] .')';
  }

  if (!empty($spec['unsigned'])) {
    $sql .= ' UNSIGNED';
  }

  if (!empty($spec['not null'])) {
    $sql .= ' NOT NULL';
  }

  if ($spec['type'] == 'serial') {
    $sql .= ' AUTO_INCREMENT';
  }

  if (isset($spec['default'])) {
    $sql .= _db_process_sql(' DEFAULT '. db_type_placeholder($spec['type']), $spec['default']);
  }

  return $sql;
}

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
      'int:tiny' => 'TINYINT',
      'int:small' => 'SMALLINT',
      'int:medium' => 'MEDIUMINT',
      'int:big' => 'BIGINT',
      'int:normal' => 'INT',

      'float:tiny' => 'FLOAT',
      'float:small' => 'FLOAT',
      'float:medium' => 'FLOAT',
      'float:big' => 'DOUBLE',
      'float:normal' => 'FLOAT',

      'numeric:normal' => 'DECIMAL',

      'serial:tiny' => 'TINYINT UNSIGNED',
      'serial:small' => 'SMALLINT UNSIGNED',
      'serial:medium' => 'MEDIUMINT UNSIGNED',
      'serial:big' => 'BIGINT UNSIGNED',
      'serial:normal' => 'INT UNSIGNED',

      'varchar:normal' => 'VARCHAR',
      'char:normal' => 'CHAR',

      'text:tiny' => 'TINYTEXT',
      'text:small' => 'TINYTEXT',
      'text:medium' => 'MEDIUMTEXT',
      'text:big' => 'LONGTEXT',
      'text:normal' => 'TEXT',

      'blob:big' => 'LONGBLOB',
      'blob:normal' => 'BLOB',

      'datetime:normal' => 'DATETIME',
    );
  }

  return $map;
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

  if (!empty($new_keys)) {
    $query .= ', ADD '. implode(', ADD ', _db_create_keys_sql($new_keys));
  }

  $ret[] = update_sql($query, $table);

  if (isset($spec['initial'])) {
    $ret[] = update_sql('UPDATE {%l} SET %l = '. db_type_placeholder($spec['type']), $table, $field, $spec['initial']);
  }

  if ($fix_null) {
    $spec['not null'] = TRUE;
    db_change_field($ret, $table, $field, $field, $spec);
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
  $ret[] = update_sql('ALTER TABLE {%l} DROP PRIMARY KEY', $table);
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
  $ret[] = update_sql('ALTER TABLE {%l} ADD UNIQUE KEY %l ('. _db_create_key_sql($fields) .')', $table, $key);
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
  $ret[] = update_sql('ALTER TABLE {%l} DROP KEY %l', $table, $key);
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
  $ret[] = update_sql('ALTER TABLE {%l} ADD INDEX %l ('. _db_create_key_sql($fields) .')', $table, $index);
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
  $ret[] = update_sql('ALTER TABLE {%l} DROP INDEX %l', $table, $index);
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
  $query = 'ALTER TABLE {%l} CHANGE %l '. _db_create_field_sql($new_field, $spec);

  if (!empty($new_keys)) {
    $query .= ', ADD '. implode(', ADD ', _db_create_keys_sql($new_keys));
  }

  $ret[] = update_sql($query, $table, $field);
}

/**
 * Returns the last insert id. This function is non thread safe.
 *
 * @param string $table
 *   The name of the table you inserted into.
 * @param string $field
 *   The name of the autoincrement field.
 * @return int
 *   The auto generated id used in the last query.
 */
function db_last_insert_id($table, $field) {
  return db_result(db_query('SELECT LAST_INSERT_ID()'));
}

/**
 * Lock a table.
 *
 * @param string $table
 *   The name of the table to lock.
 */
function db_lock_table($table) {
  db_query('LOCK TABLES {%l} WRITE', $table);
}

/**
 * Unlock all locked tables.
 */
function db_unlock_tables() {
  db_query('UNLOCK TABLES');
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
  return (bool) db_fetch_object(db_query("SHOW TABLES LIKE '{%l}'", $table));
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
  return (bool) db_fetch_object(db_query("SHOW COLUMNS FROM {%l} LIKE '%l'", $table, $field));
}
