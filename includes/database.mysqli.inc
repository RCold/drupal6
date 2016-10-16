<?php

/**
 * @file
 * Database interface code for MySQL database servers using the mysqli client
 * libraries. mysqli is included in PHP 5 by default and allows developers to
 * use the advanced features of MySQL 4.1.x, 5.0.x and beyond.
 */

// Maintainers of this file should consult:
// http://www.php.net/manual/en/ref.mysqli.php

/**
 * @ingroup database
 * @{
 */

// Include functions shared between mysql and mysqli.
require_once './includes/database.mysql-common.inc';

/**
 * Returns the version of the database server currently in use.
 *
 * @return string
 *   Database server version
 */
function db_version() {
  global $active_db;
  list($version) = explode('-', mysqli_get_server_info($active_db));
  return $version;
}

/**
 * Initialize a database connection.
 *
 * Note that mysqli does not support persistent connections.
 *
 * @param string $url
 *   The database connection URL.
 *
 * @return mysqli
 *   The database connection.
 */
function db_connect($url) {
  global $db_collation;

  // Check if MySQLi support is present in PHP
  if (!function_exists('mysqli_init')) {
    _db_error_page('Unable to use the MySQLi database because the MySQLi extension for PHP is not installed. Check your <code>php.ini</code> to see how you can enable it.');
  }

  $url_parts = parse_url($url);
  // Decode urlencoded information in the database connection URL
  $db_host = urldecode($url_parts['host']);
  $db_user = urldecode($url_parts['user']);
  // Test if database URL has a password.
  $db_pass = isset($url_parts['pass']) ? urldecode($url_parts['pass']) : '';
  $db_name = substr(urldecode($url_parts['path']), 1);
  $db_port = isset($url_parts['port']) ? $url_parts['port'] : NULL;

  $db_conn = mysqli_init();
  @mysqli_real_connect($db_conn, $db_host, $db_user, $db_pass, $db_name, $db_port, NULL, MYSQLI_CLIENT_FOUND_ROWS);

  if (mysqli_connect_errno() > 0) {
    _db_error_page(mysqli_connect_error());
  }

  // Force MySQL to use the UTF-8 character set. Also set the collation, if a
  // certain one has been set; otherwise, MySQL defaults to 'utf8_general_ci'
  // for UTF-8.
  if (!empty($db_collation)) {
    mysqli_query($db_conn, "SET NAMES 'utf8' COLLATE '". mysqli_real_escape_string($db_conn, $db_collation) ."'");
  }
  else {
    mysqli_query($db_conn, "SET NAMES 'utf8'");
  }

  return $db_conn;
}

/**
 * Helper function for db_query().
 */
function _db_query($query, $debug = 0) {
  global $active_db, $queries, $user;

  if (variable_get('dev_query', 0)) {
    list($usec, $sec) = explode(' ', microtime());
    $timer = (float) $usec + (float) $sec;
    // If devel.module query logging is enabled, prepend a comment with the username and calling function
    // to the SQL string. This is useful when running mysql's SHOW PROCESSLIST to learn what exact
    // code is issueing the slow query.
    $bt = debug_backtrace();
    // t() may not be available yet so we don't wrap 'Anonymous'.
    $name = $user->uid ? $user->name : variable_get('anonymous', 'Anonymous');
    // str_replace() to prevent SQL injection via username or anonymous name.
    $name = str_replace(array('*', '/'), '', $name);
    $query = '/* '. $name .' : '. $bt[2]['function'] .' */ '. $query;
  }

  $result = mysqli_query($active_db, $query);

  if (variable_get('dev_query', 0)) {
    $query = $bt[2]['function'] ."\n". $query;
    list($usec, $sec) = explode(' ', microtime());
    $stop = (float) $usec + (float) $sec;
    $diff = $stop - $timer;
    $queries[] = array($query, $diff);
  }

  if ($debug) {
    print '<p>query: '. $query .'<br />error:'. mysqli_error($active_db) .'</p>';
  }

  if (mysqli_errno($active_db) == 0) {
    return $result;
  }
  else {
    // Indicate to drupal_error_handler that this is a database error.
    ${DB_ERROR} = TRUE;
    trigger_error(check_plain(mysqli_error($active_db) ."\nquery: ". $query), E_USER_WARNING);
    return FALSE;
  }
}

/**
 * Fetch one result row from the previous query as an object.
 *
 * @param mysqli_result $result
 *   A database query result resource, as returned from db_query().
 *
 * @return object|bool
 *   An object representing the next row of the result, or FALSE. The attributes
 *   of this object are the table fields selected by the query.
 */
function db_fetch_object($result) {
  $object = mysqli_fetch_object($result);
  return isset($object) ? $object : FALSE;
}

/**
 * Fetch one result row from the previous query as an array.
 *
 * @param mysqli_result $result
 *   A database query result resource, as returned from db_query().
 *
 * @return array|bool
 *   An associative array representing the next row of the result, or FALSE.
 *   The keys of this object are the names of the table fields selected by the
 *   query, and the values are the field values for this result row.
 */
function db_fetch_array($result) {
  $array = mysqli_fetch_array($result, MYSQLI_ASSOC);
  return isset($array) ? $array : FALSE;
}

/**
 * Return an individual result field from the previous query.
 *
 * Only use this function if exactly one field is being selected; otherwise,
 * use db_fetch_object() or db_fetch_array().
 *
 * @param mysqli_result $result
 *   A database query result resource, as returned from db_query().
 *
 * @return mixed
 *   The resulting field or FALSE.
 */
function db_result($result) {
  if (mysqli_num_rows($result) > 0) {
    // The mysqli_fetch_row function has an optional second parameter $row
    // but that can't be used for compatibility with Oracle, DB2, etc.
    $array = mysqli_fetch_row($result);
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
  return mysqli_errno($active_db);
}

/**
 * Determine the number of rows changed by the preceding query.
 *
 * @return int
 *   The number of affected rows on success, and -1 if the last query failed.
 */
function db_affected_rows() {
  global $active_db;
  return mysqli_affected_rows($active_db);
}

/**
 * Returns a properly formatted Binary Large Object value.
 *
 * @param string $data
 *   Data to encode.
 *
 * @return string
 *   Encoded data.
 */
function db_encode_blob($data) {
  global $active_db;
  return "'". mysqli_real_escape_string($active_db, $data) ."'";
}

/**
 * Returns text from a Binary Large Object value.
 *
 * @param string $data
 *   Data to decode.
 *
 * @return string
 *   Decoded data.
 */
function db_decode_blob($data) {
  return $data;
}

/**
 * Prepare user input for use in a database query, preventing SQL injection
 * attacks.
 *
 * @param string $text
 *   The string to escape.
 *
 * @return string
 *   The escaped string.
 */
function db_escape_string($text) {
  global $active_db;
  return mysqli_real_escape_string($active_db, $text);
}

/**
 * @} End of "ingroup database".
 */
