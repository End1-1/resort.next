<?php
session_start();

// Credentials come from the environment. See env.example. Do not hard-code them.
$servername = getenv("SMARTHOTEL_DB_HOST");
$username = getenv("SMARTHOTEL_DB_USER");
$password = getenv("SMARTHOTEL_DB_PASSWORD");
$database = getenv("SMARTHOTEL_DB_NAME");
if ($servername === false || $servername === ""
    || $username === false || $username === ""
    || $password === false
    || $database === false || $database === "") {
    die("Database is not configured");
}

$dbconn = new mysqli($servername, $username, $password, $database);
if ($dbconn->connect_error) {
    die("Database connection failed");
}
$dbconn->set_charset("utf8");

$SHOW_MENU = false;
