<?php

/**
 * @file
 * Database interface code for MySQL database servers.
 */

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
  list($version) = explode('-', mysql_get_server_info());
  return $version;
}

/**
 * Initialize a database connection.
 *
 * @param string $url
 *   The database connection URL.
 *
 * @return resource
 *   The database connection.
 */
function db_connect($url) {
  global $db_collation;

  // Check if MySQL support is present in PHP
  if (!function_exists('mysql_connect')) {
    _db_error_page('Unable to use the MySQL database because the MySQL extension for PHP is not installed. Check your <code>php.ini</code> to see how you can enable it.');
  }

  $url_parts = parse_url($url);
  // Decode urlencoded information in the database connection URL
  $db_host = urldecode($url_parts['host']);
  $db_user = urldecode($url_parts['user']);
  // Test if database URL has a password.
  $db_pass = isset($url_parts['pass']) ? urldecode($url_parts['pass']) : '';
  $db_name = substr(urldecode($url_parts['path']), 1);

  // Allow for non-standard MySQL port.
  if (isset($url_parts['port'])) {
    $db_host .= ':'. $url_parts['port'];
  }

  // - TRUE makes mysql_connect() always open a new link, even if
  //   mysql_connect() was called before with the same parameters.
  //   This is important if you are using two databases on the same
  //   server.
  // - 2 means CLIENT_FOUND_ROWS: return the number of found
  //   (matched) rows, not the number of affected rows.
  $db_conn = @mysql_connect($db_host, $db_user, $db_pass, TRUE, 2);
  if (!$db_conn || !mysql_select_db($db_name)) {
    // Show error screen otherwise
    _db_error_page(mysql_error());
  }

  // Force MySQL to use the UTF-8 character set. Also set the collation, if a
  // certain one has been set; otherwise, MySQL defaults to 'utf8_general_ci'
  // for UTF-8.
  if (isset($db_collation) && is_string($db_collation)) {
    mysql_query("SET NAMES 'utf8' COLLATE '". mysql_real_escape_string($db_collation, $db_conn) ."'", $db_conn);
  }
  else {
    mysql_query("SET NAMES 'utf8'", $db_conn);
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

  $result = mysql_query($query, $active_db);

  if (variable_get('dev_query', 0)) {
    $query = $bt[2]['function'] ."\n". $query;
    list($usec, $sec) = explode(' ', microtime());
    $stop = (float) $usec + (float) $sec;
    $diff = $stop - $timer;
    $queries[] = array($query, $diff);
  }

  if ($debug) {
    print '<p>query: '. $query .'<br />error:'. mysql_error($active_db) .'</p>';
  }

  if (mysql_errno($active_db) == 0) {
    return $result;
  }
  else {
    // Indicate to drupal_error_handler that this is a database error.
    ${DB_ERROR} = TRUE;
    trigger_error(check_plain(mysql_error($active_db) ."\nquery: ". $query), E_USER_WARNING);
    return FALSE;
  }
}

/**
 * Fetch one result row from the previous query as an object.
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 *
 * @return object|bool
 *   An object representing the next row of the result, or FALSE. The attributes
 *   of this object are the table fields selected by the query.
 */
function db_fetch_object($result) {
  return mysql_fetch_object($result);
}

/**
 * Fetch one result row from the previous query as an array.
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 *
 * @return array|bool
 *   An associative array representing the next row of the result, or FALSE.
 *   The keys of this object are the names of the table fields selected by the
 *   query, and the values are the field values for this result row.
 */
function db_fetch_array($result) {
  return mysql_fetch_array($result, MYSQL_ASSOC);
}

/**
 * Return an individual result field from the previous query.
 *
 * Only use this function if exactly one field is being selected; otherwise,
 * use db_fetch_object() or db_fetch_array().
 *
 * @param resource $result
 *   A database query result resource, as returned from db_query().
 *
 * @return mixed
 *   The resulting field or FALSE.
 */
function db_result($result) {
  if (mysql_num_rows($result) > 0) {
    // The mysql_fetch_row function has an optional second parameter $row
    // but that can't be used for compatibility with Oracle, DB2, etc.
    $array = mysql_fetch_row($result);
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
  return mysql_errno($active_db);
}

/**
 * Determine the number of rows changed by the preceding query.
 *
 * @return int
 *   The number of affected rows on success, and -1 if the last query failed.
 */
function db_affected_rows() {
  global $active_db;
  return mysql_affected_rows($active_db);
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
  return "'". mysql_real_escape_string($data, $active_db) ."'";
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
  return mysql_real_escape_string($text, $active_db);
}

/**
 * @} End of "ingroup database".
 */
