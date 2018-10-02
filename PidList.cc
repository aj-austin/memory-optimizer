#include "PidList.h"
#include <dirent.h>
#include <string>
#include <cstdlib>
#include <string.h>

int PidList::collect()
{
  DIR *dir;
  struct dirent *dirent;
  int rc = 0;

  dir = opendir("/proc/");
  if (!dir)
    return -ENOENT;

  for(;;) {
    errno = 0;
    rc = 0;
    dirent = readdir(dir);
    if (!dirent) {
      if (0 != errno)
        rc = errno;

      break;
    }

    if (DT_DIR != dirent->d_type
        || !is_digit(dirent->d_name))
      continue;

    rc = parse_one_pid(dirent);
    if (rc < 0) {
      fprintf(stderr, "parse_one_pid failed. name: %s err: %d\n",
              dirent->d_name, rc);
      break;
    }
  }

  closedir(dir);

  return rc;
}


int PidList::parse_one_pid(struct dirent* proc_ent)
{
  int rc;
  FILE *file;
  char filename[PATH_MAX];

  snprintf(filename, sizeof(filename), "/proc/%s/status", proc_ent->d_name);
  file = fopen(filename, "r");
  if (!file) {
    fprintf(stderr, "open %s failed\n", filename);
    return errno;
  }

  rc = do_parse_one_pid(file, proc_ent);

  fclose(file);

  return rc;
}

int PidList::do_parse_one_pid(FILE *file, struct dirent* proc_ent)
{
  int rc;
  char line[4096];
  struct PidItem new_pid_item = {};

  while(fgets(line, sizeof(line), file)) {
    rc = do_parse_one_line(new_pid_item, proc_ent, line);
    if (rc < 0)
      break;
  }

  if (rc >= 0)
    rc = save_into_pid_set(new_pid_item);

  return rc;
}

int PidList::do_parse_one_line(struct PidItem &new_pid_item,
                               struct dirent* proc_ent, char* line_ptr)
{
  int rc;
  char* name_ptr;
  char* value_ptr;

  rc = get_field_name_value(line_ptr, &name_ptr, &value_ptr);
  if (rc < 0)
    return rc;

  do {
      // a map from string to parser function ptr is better
      // if we need to parse more fields, but now just 3 here
      // so let's keep things simple.
      if (!strcmp("Pid", name_ptr)) {
          parse_value_number_1(value_ptr, new_pid_item.pid);
          break;
      }

      if (!strcmp("Name", name_ptr)) {
        parse_value_string_1(value_ptr, new_pid_item.name);
        break;
      }

      if (!strcmp("RssAnon", name_ptr)) {
        parse_value_number_with_unit(value_ptr, new_pid_item.rss_anon);
        break;
      }

      // add more here if necessary

  } while(0);

  return rc;
}

int PidList::get_field_name_value(char *field_ptr,
                                  char **name_ptr, char **value_ptr)
{
  int split_index = -1;

  sscanf(field_ptr, "%*[^:]%n", &split_index);
  if (split_index > 0) {
    field_ptr[split_index] = 0;
    *name_ptr = field_ptr;
    *value_ptr = field_ptr + split_index + 1;

    return 0;
  }

  return -PARSE_FIELD_NAME_VALUE_FAILED;
}

int PidList::save_into_pid_set(PidItem& new_pid_item)
{
  try {
    pid_set.push_back(new_pid_item);
  } catch (std::bad_alloc& e) {
    return -ENOMEM;
  }

  return 0;
}

void PidList::parse_value_number_with_unit(char* value_ptr,
                                           unsigned long &out_value)
{
  int scan_ret;
  int num_value;
  char unit[16];

  scan_ret = sscanf(value_ptr, "%*[\t]%*[ ]%d %s",
                    &num_value,
                    unit);
  if (scan_ret < 1) {
    out_value = 0;
    return;
  }

  out_value = num_value;
  if (!strcmp(unit, "kB"))
    out_value <<= 10;

  return;
}

void PidList::parse_value_string_1(char* value_ptr,
                                   std::string &out_value)
{
  int name_index = -1;

  sscanf(value_ptr, "\t%n",
         &name_index);

  if (name_index <= 0) {
    out_value = "";
    return;
  }

  out_value = value_ptr + name_index;
  out_value.pop_back(); // trim '\n'

  return;
}

void PidList::parse_value_number_1(char* value_ptr,
                                   unsigned long &out_value)
{
  std::string value_str;

  parse_value_string_1(value_ptr, value_str);
  out_value = strtoul(value_str.c_str(), NULL, 0);
}


#ifdef PID_LIST_SELF_TEST

int main(int argc, char* argv[])
{
  PidList pl;
  int err;

  err = pl.collect();
  if (err) {
    fprintf(stderr, "get pid list failed! err = %d\n", err);
    return err;
  }

  setlocale(LC_NUMERIC, "");

  printf("\nList all pids:\n");
  for(auto &item : pl.get_pidlist()) {
    printf("PID: %8lu  RssAnon: %'15lu  name: %s\n",
           item.pid,
           item.rss_anon,
           item.name.c_str());
    }

  printf("\nList kthreadd by name:\n");
  for(auto &item : pl.get_pidlist()) {
    if (pl.is_name(item, "kthreadd"))
      printf("PID: %8lu  RssAnon: %'15lu  name: %s\n",
             item.pid,
             item.rss_anon,
             item.name.c_str());
  }

  printf("\nList pids only have RssAnon:\n");
  for(auto &item : pl.get_pidlist()) {
    if (pl.is_have_rss_anon(item))
      printf("PID: %8lu  RssAnon: %'15lu  name: %s\n",
             item.pid,
             item.rss_anon,
             item.name.c_str());
  }

  return 0;
}

#endif
