<?php

/**
 * @file
 *   Ding utility functions.
 */

/**
 * Implements hook_drush_command().
 */
function ding2_utils_drush_command() {
  $items = array();

  $items['ding2-changed'] = array(
    'description' => "Check which modules has been updated.",
    'options' => array(
      'summary' => 'Only show updated.'
    ),
    /* 'arguments' => array( */
    /*   'site-alias' => 'The site alias to install to', */
    /* ), */
    /* 'examples' => array( */
    /*   'drush ding-deploy-install @staging' => 'Install on the @staging server.', */
    /* ), */
    /* 'aliases' => array('ddinstall'), */
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH, // No bootstrap at all.
    /* 'callback' => "ding2_utils_changed", */
  );

  $items['ding2-update'] = array(
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['ding2-test'] = array(
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['ding2-info-make-check'] = array(
    'description' => 'Check that make files contains the modules listed as dependencies in the info file.',
    'aliases' => array('imc'),
    'options' => array(
      'summary' => 'Only show missing.'
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['ding2-make-check'] = array(
    'description' => 'Check that all makefiles fetches the same version.',
    'aliases' => array('mc'),
    /* 'options' => array( */
    /*   'summary' => 'Only show missing.' */
    /* ), */
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  return $items;
}

function drush_ding2_utils_ding2_changed() {
  foreach (_ding2_utils_changed() as $module => $status) {
    if ($status['changed']) {
      drush_log($module, 'ok');
      drush_print($status['log']);
      drush_print_pipe($module);
    }
    else {
      if (!drush_get_option('summary', FALSE)){
        drush_log(dt("No changes to @module since @version", array('@module' => $module, '@version' => $status['version'])), 'ok');
      }
    }
  }
}

function drush_ding2_utils_ding2_update() {
  // Find changed modules.
  $modules = _ding2_utils_changed();
  $module_names = array_keys($modules);
  foreach ($modules as $name => &$status) {
    if ($status['changed']) {
      $status['dependants'] = _ding2_utils_dependants($name, $module_names);
      foreach ($status['dependants'] as $dependant) {
        if (isset($modules[$dependant])) {
          // Only set depends when not changed.
          if (!$modules[$dependant]['changed']) {
            $modules[$dependant]['depends'] = TRUE;
          }
          $modules[$dependant]['changed'] = TRUE;
          if (!isset($modules[$dependant]['dependencies'])) {
            $modules[$dependant]['dependencies'] = array();
          }
          $modules[$dependant]['dependencies'][] = $name;
        }
      }
    }
  }

  // Display list.
  foreach ($modules as $name => $status) {
    if ($status['changed']) {
      drush_log($name . (isset($status['depends']) ? ' (depends)' : ' (changed)'), 'ok');
    }
  }

  foreach ($modules as $name => $status) {
    if (!empty($status['log'])) {
      drush_print($name . ":");
      drush_print($status['log']);
    }
  }

  if (!drush_confirm('Continue?')) {
    return drush_user_abort();
  }

  // Check that current versions match info file, and figure out what the
  // next is.
  foreach ($modules as $name => &$status) {
    // Check current.
    $info = _ding2_utils_parse_info_file($name . '/' . $name . '.info');

    // If version is not set, set it to undefined for later print.
    if (empty($info['version'])) {
      $info['version'] = 'undefined';
    }
    if ($info['version'] != $status['version']) {
      drush_log(dt('Version @version different from tag @tag for @module.', array('@module' => $name, '@version' => $info['version'], '@tag' => $status['tag'])), 'warning');
      if ($status['changed']) {
        drush_log(dt('Version will be fixed.'), 'info');
      }
    }
    // Figure out the next.
    if ($status['changed']) {
      // Transision oldstyle v0.1 tags to 7.x-0.1 tags.
      $version = preg_replace('/^[^\d]+/', '', $status['tag']);
      if (!preg_match('/^7.x-/', $version)) {
        $version = '7.x-' . $version;
      }
      $v = explode('.', $version);
      $v[sizeof($v)-1]++;
      // Also, now the version and tag is the same string.
      $status['new_version'] = $status['new_tag'] = implode('.', $v);
    }
  }

  // Update modules.
  foreach ($modules as $name => $status) {
    chdir($name);
    // Update make file.
    if (!empty($status['dependencies'])) {
      $make_file_name = $name . '.make';
      $make_file = file_get_contents($make_file_name);
      foreach ($status['dependencies'] as $dependency) {
        $dep_status = $modules[$dependency];
        if (!$dep_status) {
          drush_log('Could not find status for dependency @module', array('@module' => $dependency), 'error');
        }
        $make_file = _ding2_utils_makefile_replace($make_file, array(
                       $dependency => $dep_status['new_tag'],
                     ));
      }
      if (drush_get_context('DRUSH_SIMULATE')) {
        drush_log($make_file, 'debug');
      }
      else {
        file_put_contents($make_file_name, $make_file);
      }

      // Commit changes.
      drush_op_system('git add ' . $make_file_name);
      $deps = $status['dependencies'];
      $last = array_pop($deps);
      if (sizeof($deps)) {
        $message = "Updated " . implode(", ", $deps) . " and " . $last . ".";
      }
      else {
        $message = "Updated " . $last . ".";
      }
      drush_op_system('git commit -m"' . $message . '"');
    }

    // Update info file.
    if ($status['changed']) {
      $info_file_name = $name . '.info';
      $info_file = file_get_contents($info_file_name);
      $info_file = preg_replace('/^\s*version\s*=.*$/m', 'version = "' . $status['new_version']. '"', $info_file);
      if (drush_get_context('DRUSH_SIMULATE')) {
        drush_log($info_file, 'debug');
      }
      else {
        file_put_contents($info_file_name, $info_file);
      }

      // Commit changes.
      drush_op_system('git add ' . $info_file_name);
      drush_op_system('git commit -m"Bumping version to ' . $status['new_version'] . '"');
      $tag = $status['tag'];
      $modules[$name]['log'] = `git log --oneline $tag..HEAD`;

      // Tag version.
      drush_op_system('git tag ' . $status['new_tag']);
    }
    chdir('..');
  }

  /* if (drush_confirm('Fix ding2?')) { */
  /*   $replacements = array(); */
  /*   $log_message = ''; */
  /*   chdir('ding2'); */
  /*   drush_op_system('git checkout dev'); */
  /*   foreach ($modules as $name => $status) { */
  /*     if ($status['changed']) { */
  /*       $replacements[$name] = $status['new_tag']; */
  /*     } */
  /*   } */
  /* } */

  $log_message = '';
  $changed = array();
  foreach ($modules as $name => $status) {
    if ($status['changed']) {
      drush_print(dt('cd @module ; git push origin master @new_tag ; cd ..', array('@module' => $name, '@new_tag' => $status['new_tag'])));
      $log_message .= $name . ":\n" . $status['log'] . "\n";
      $changed[] = $name;
    }
  }
  sort($changed);
  $last = array_pop($changed);
  if (sizeof($changed)) {
    $message = "Updated " . implode(", ", $changed) . " and " . $last . ".";
  }
  else {
    $message = "Updated " . $last . ".";
  }

  drush_print($message . "\n\nDetails:\n\n" . $log_message);
}

/**
 *
 */
function drush_ding2_utils_ding2_info_make_check() {
  // Ignore these submodules.
  $ignore = array(
    'connie',
    'test_facetbrowser_search',
    'ding_periodical_reservation',
  );
  $mappings = array(
    'ding_periodical_reservation' => 'ding_periodical',

    'page_manager' => 'ctools',
    'i18n_field' => 'i18n',
    'date_popup' => 'date',
    'block' => NULL,
    'contextual' => NULL,
    'image' => NULL,
    'locale' => NULL,
    'menu' => NULL,
    'dblog' => NULL,
    'shortcut' => NULL,
    'toolbar' => NULL,
    'overlay' => NULL,
    'color' => NULL,
    'search' => NULL,
    'translation' => NULL,
    'list' => NULL,
  );
  $files = drush_scan_directory(drush_cwd(), '/\.info$/', array('.', '..', 'CVS', 'contrib'));
  foreach ($files as $file) {
    if (in_array(basename($file->filename, '.info'), $ignore)) {
      // Skip ignored modules.
      continue;
    }
    $ini = _ding2_utils_parse_info_file($file->filename);
    $module = basename($file->filename, '.info');
    if ($ini && isset($ini['dependencies'])) {
      $ok = TRUE;
      $deps = array();
      $bad = array();
      $make_file = "";
      $make_filename = dirname($file->filename) . '/' . $module . '.make';
      if (file_exists($make_filename)) {
        $make_file = file_get_contents($make_filename);
      }
      foreach ($ini['dependencies'] as $dependency) {
        $deps[] = $dependency;
        if (array_key_exists($dependency, $mappings)) {
          $dependency = $mappings[$dependency];
          if (!$dependency) {
            continue;
          }
        }
        if (!preg_match('/' . preg_quote($dependency) . '/', $make_file)) {
          $ok = FALSE;
          $bad[] = $dependency;
        }
      }
      if (!drush_get_option('summary', FALSE) || !$ok){
        drush_log($module . ': ' . join(', ', $deps), $ok ? 'ok' : 'error');
      }
      if (!$ok) {
        $bad = array_unique($bad);
        drush_log('  missing: ' . join(', ', $bad), 'error');
      }
    }
  }
}

/**
 *
 */
function drush_ding2_utils_ding2_make_check() {
  $dl_types = array(
    'url' => 'url',
    'tag' => 'tags',
    'patch' => 'patches',
  );
  $seen_projects = array();
  $files = drush_scan_directory(drush_cwd(), '/\.make$/', array('.', '..', 'CVS', 'contrib'));
  foreach ($files as $file) {
    $current_module = basename($file->filename, '.make');
    $makefile = _ding2_utils_parse_info_file($file->filename);
    foreach ($makefile['projects'] as $name => $project) {
      if (!isset($seen_projects[$name])) {
        $project['mc_modules'] = array($current_module);
        $seen_projects[$name] = $project;
      }
      else {
        $seen_projects[$name]['mc_modules'][] = $current_module;
        $existing = $seen_projects[$name];
        $in_modules = join(', ', $existing['mc_modules']);
        /* drush_print_r($existing); */
        if (!isset($existing['download'])) {
          // Regular project, compare versions and patches.
          if ($existing['version'] != $project['version']) {
            drush_log(dt('Different versions for @project in @module. (@modules)', array('@project' => $name, '@module' => $current_module, '@modules' => $in_modules)), 'error');
          }
          if ((isset($existing['patch']) || isset($project['patch'])) && $existing['patch'] != $project['patch']) {
            drush_log(dt('Different patches for @project in @module. (@modules)', array('@project' => $name, '@module' => $current_module, '@modules' => $in_modules)), 'error');
          }
        }
        else {
          // Non-d.o project.
          $error = FALSE;
          foreach ($dl_types as $key => $desc) {
            if ($existing['download'][$key] != $project['download'][$key]){
              drush_log(dt('Different @type for @project in @module. (@modules)', array('@project' => $name, '@module' => $current_module, '@type' => $desc, '@modules' => $in_modules)), 'error');
              $error = TRUE;
            }
          }
          if (!$error) {
            if ($existing['download'] != $project['download']) {
              drush_log(dt('Different download for @project in @module. (@modules)', array('@project' => $name, '@module' => $current_module, '@modules' => $in_modules)), 'error');
            }
          }
        }
      }
    }
    // drush_print_r($makefile);
  }
}

/**
 *
 */
function _ding2_utils_makefile_replace($make_file, $replacements) {
  foreach ($replacements as $dependency => $tag) {
    $pattern = "/^\\s*;?\\s*projects\\[$dependency\\]\\[download\\]\\[tag\\].*$/m";
    // Remove any old line(s).
    $make_file = preg_replace($pattern, '', $make_file);
    // Splice in new after the last occurrence of the project.
    $new_line = 'projects[' . $dependency . '][download][tag] = "' . $tag . '"';
    $project_pos = strrpos($make_file, 'projects[' . $dependency . ']');
    if ($project_pos === FALSE) {
      $new_line_pos = FALSE;
    }
    else {
      $new_line_pos = strpos($make_file, "\n", $project_pos);
    }
    if ($new_line_pos === FALSE) {
      // Just append.
      $make_file .= $new_line;
    }
    else {
      $make_file = substr($make_file, 0, $new_line_pos + 1) . $new_line . substr($make_file, $new_line_pos + 1);
    }
  }
  return $make_file;
}

/**
 *
 */
function _ding2_utils_dependants($name, $modules) {
  $dependants = array();
  foreach ($modules as $module) {
    $make_file = $module . '/' . $module . '.make';
    if (file_exists($make_file)) {
      $content = file_get_contents($make_file);
      if (strpos($content, "projects[$name]")) {
        $dependants[] = $module;
      }
    }
  }
  return $dependants;
}

/**
 * Collect information about changed repos.
 */
function _ding2_utils_changed() {
  $modules = array();
  $files=`ls`;
  foreach (explode("\n", $files) as $dir) {
    if ($dir == 'ding2' || $dir == 'ting-client') {
      // Ignore the profile and client.
      continue;
    }
    if (is_dir($dir . '/.git')) {
      chdir($dir);
      // Look for 7.x-X tags.
      $tags = explode("\n", `git tag -l 7.x-*| sort -r`);
      if (!$tags[0]) {
        // Only fall back to oldstyle vX tags if no 7.x-X style tags exists.
        $tags = explode("\n", `git tag -l v* | sort -r`);
      }
      if (!$tags[0]) {
        drush_log(dt("No tags found in @module, ignoring.", array('@module' => $dir)), 'warning');
        chdir('..');
        continue;
      }
      $tag = $tags[0];
      $latest = preg_replace('/^[^\d]+/', '', $tag);
      $log = `git log --oneline $tag..HEAD`;

      $modules[$dir] = array(
        'tag' => $tag,
        'version' => $latest,
        'changed' => FALSE,
      );
      if ($log) {
        $modules[$dir]['changed'] = TRUE;
        $modules[$dir]['log'] = $log;
      }
      chdir('..');
    }
  }

  return $modules;
}

/**
 *
 */
function drush_ding2_utils_ding2_test() {
  /* drush_print_r(_ding2_utils_changed()); */
}

function _ding2_utils_parse_info_file($filename) {
  $info = array();
  $constants = get_defined_constants();

  if (!file_exists($filename)) {
    return $info;
  }

  $data = file_get_contents($filename);
  if (preg_match_all('
    @^\s*                           # Start at the beginning of a line, ignoring leading whitespace
    ((?:
      [^=;\[\]]|                    # Key names cannot contain equal signs, semi-colons or square brackets,
      \[[^\[\]]*\]                  # unless they are balanced and not nested
    )+?)
    \s*=\s*                         # Key/value pairs are separated by equal signs (ignoring white-space)
    (?:
      ("(?:[^"]|(?<=\\\\)")*")|     # Double-quoted string, which may contain slash-escaped quotes/slashes
      (\'(?:[^\']|(?<=\\\\)\')*\')| # Single-quoted string, which may contain slash-escaped quotes/slashes
      ([^\r\n]*?)                   # Non-quoted string
    )\s*$                           # Stop at the next end of a line, ignoring trailing whitespace
    @msx', $data, $matches, PREG_SET_ORDER)) {
    foreach ($matches as $match) {
      // Fetch the key and value string
      $i = 0;
      foreach (array('key', 'value1', 'value2', 'value3') as $var) {
        $$var = isset($match[++$i]) ? $match[$i] : '';
      }
      $value = stripslashes(substr($value1, 1, -1)) . stripslashes(substr($value2, 1, -1)) . $value3;

      // Parse array syntax
      $keys = preg_split('/\]?\[/', rtrim($key, ']'));
      $last = array_pop($keys);
      $parent = &$info;

      // Create nested arrays
      foreach ($keys as $key) {
        if ($key == '') {
          $key = count($parent);
        }
        if (!isset($parent[$key]) || !is_array($parent[$key])) {
          $parent[$key] = array();
        }
        $parent = &$parent[$key];
      }

      // Handle PHP constants.
      if (isset($constants[$value])) {
        $value = $constants[$value];
      }

      // Insert actual value
      if ($last == '') {
        $last = count($parent);
      }
      $parent[$last] = $value;
    }
  }

  return $info;
}
