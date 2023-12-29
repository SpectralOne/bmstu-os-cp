#include <linux/input.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#define DEVICE_NAME "touchpad_gestures"
#define PROCFS_MAX_SIZE 1000
#define MAX_GEST_CNT 6
#define MAX_BUFF_SIZE 350

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Artyom Bogachenko");
MODULE_DESCRIPTION("Touchpad gestures");
MODULE_VERSION("1.0.0");

static int count_x = 0;
static int count_y = 0;
static int position_x[MAX_BUFF_SIZE] = {0};
static int position_y[MAX_BUFF_SIZE] = {0};
static int y_end, x_end;
char proc_buf[PROCFS_MAX_SIZE] = {0};

static char *argv[3];
static char *envp[] = {"HOME=/root",
                       "TERM=linux",
                       "USER=root",
                       "DISPLAY=:1",
                       "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/0/bus",
                       "XDG_CONFIG_DIRS=/etc/xdg/xdg-ubuntu:/etc/xdg",
                       "XDG_RUNTIME_DIR=/run/user/0",
                       "XDG_SESSION_TYPE=x11",
                       "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
                       NULL};

static struct state {
  char execute[MAX_BUFF_SIZE];
} cmd_state[MAX_GEST_CNT];

struct work_arg_struct {
  struct work_struct work;
  int data;
};

static struct work_arg_struct my_work;

static void parse_pattern(struct work_struct *work) {
  int result = 0;

  argv[0] = "/usr/bin/zsh";
  argv[2] = NULL;
  if (abs(position_x[0] - x_end) <= 200 && abs(y_end - position_y[0]) > 700) {
    int up = position_y[0] > y_end ? 1 : 0;
    printk("touchpad_gestures.c: Verical Line, cmd = %s", up ? cmd_state[0].execute : cmd_state[1].execute);
    argv[1] = up ? cmd_state[0].execute : cmd_state[1].execute;
    result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  } else if (abs(y_end - position_y[0]) <= 200 &&
             abs(position_y[count_y / 2] - position_y[0]) <= 100 &&
             abs(position_x[0] - x_end) > 700) {
    printk("touchpad_gestures.c: Horizonal Line %s", cmd_state[2].execute);
    argv[1] = cmd_state[2].execute;
    result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  } else if (abs(position_x[0] - x_end) > 700 &&
             (position_y[count_y / 2] > position_y[0]) &&
             abs(y_end - position_y[0]) <= 200) {
    printk("touchpad_gestures.c: V shape %s", cmd_state[3].execute);
    argv[1] = cmd_state[3].execute;
    result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  } else if ((position_x[0] > x_end) && (position_y[0] < y_end)) {
    printk("touchpad_gestures.c: Right Diagonal %s", cmd_state[4].execute);
    argv[1] = cmd_state[4].execute;
    result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  } else if ((position_x[0] < x_end) && (position_y[0] > y_end)) {
    printk("touchpad_gestures.c: Left Diagonal %s", cmd_state[5].execute);
    argv[1] = cmd_state[5].execute;
    result = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
  } else {
    printk("touchpad_gestures.c: unknown gesture");
  }
  printk("touchpad_gestures.c: execute status = %d", result);

  count_x = 0;
  count_y = 0;
  for (int i = 0; i < MAX_BUFF_SIZE; ++i) position_x[i] = 0;
  for (int i = 0; i < MAX_BUFF_SIZE; ++i) position_y[i] = 0;
}

static struct proc_dir_entry *proc_file;

bool connected = false;
bool start_record = false;
int tracking_id = -1;

static bool touchpad_gest_filter(struct input_handle *handle, unsigned int type,
                                 unsigned int code, int value) {
  if (code == SYN_REPORT) return 0;

  if (code == ABS_MT_SLOT) {
    bool slot_matched = (value == 2);

    if (!connected) {
      connected = slot_matched;
    }

    if (connected) {
      start_record = slot_matched;
    }

    return 0;
  }

  if (start_record && code == ABS_MT_TRACKING_ID) {
    if (tracking_id == -1) {
      printk("touchpad_gestures.c: started pattern recording");
      tracking_id = value;
      return 0;
    }

    if (value == -1) {
      tracking_id = value;
      connected = false;
      start_record = false;
      printk("touchpad_gestures.c: pattern recording complete");
      schedule_work(&my_work.work);
      return 0;
    }

    return 0;
  }

  if (start_record && code == ABS_MT_POSITION_X) {
    if (count_x < MAX_BUFF_SIZE) {
      position_x[count_x++] = value;
    }
    x_end = value;
  }

  if (start_record && code == ABS_MT_POSITION_Y) {
    if (count_y < MAX_BUFF_SIZE) {
      position_y[count_y++] = value;
    }
    y_end = value;
  }

  return 0;
}

static int touchpad_gest_connect(struct input_handler *handler,
                                 struct input_dev *dev,
                                 const struct input_device_id *id) {
  struct input_handle *handle;
  int error;

  handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
  if (!handle) return -ENOMEM;

  handle->dev = dev;
  handle->handler = handler;
  handle->name = "touchpad_gestures_handle";

  error = input_register_handle(handle);
  if (error) goto err_free_handle;

  error = input_open_device(handle);
  if (error) goto err_unregister_handle;

  printk(KERN_INFO "touchpad_gestures.c: connected device: (%s  %s)\n",
         dev->name, dev->phys);

  return 0;

err_unregister_handle:
  input_unregister_handle(handle);
err_free_handle:
  kfree(handle);
  return error;
}

static void touchpad_gest_disconnect(struct input_handle *handle) {
  printk(KERN_INFO "touchpad_gestures.c: disconnect %s\n", handle->dev->name);

  input_close_device(handle);
  input_unregister_handle(handle);
  kfree(handle);
}

static ssize_t proc_read(struct file *filp, char __user *buf, size_t size,
                         loff_t *off) {
  loff_t offset = *off;
  size_t remaining;

  if (offset < 0) return -EINVAL;

  if (offset >= MAX_BUFF_SIZE || size == 0) return 0;

  if (size > MAX_BUFF_SIZE - offset) size = MAX_BUFF_SIZE - offset;

  remaining = copy_to_user(buf, proc_buf + offset, size);
  if (remaining == size) {
    printk(KERN_ERR "touchpad_gestures.c: copy_to_user failed\n");
    return -EFAULT;
  }

  size -= remaining;
  *off = offset + size;
  return size;
}

static ssize_t proc_write(struct file *fp, const char *buf, size_t len,
                          loff_t *off) {
  int index = 0, idx = 0;
  if (len > PROCFS_MAX_SIZE) {
    return -EFAULT;
  }

  if (copy_from_user(proc_buf, buf, len)) {
    return -EFAULT;
  }
  for (int i = 0; i < len; ++i) {
    if (proc_buf[i] == '\n') {
      cmd_state[index].execute[idx++] = '\0';
      idx = 0;
      index++;
    } else {
      cmd_state[index].execute[idx++] = proc_buf[i];
    }
  }
  for (int i = 0; i < MAX_GEST_CNT; ++i) {
    printk("touchpad_gestures.c: cmd %d = %s", i+1, cmd_state[i].execute);
  }
  printk("touchpad_gestures.c: cmd configuration applied");

  return len;
}

static const struct input_device_id touchpad_gest_ids[] = {
    {.driver_info = 1}, /* Matches all devices */
    {},
};

static struct input_handler touchpad_gest_handler = {
    .filter = touchpad_gest_filter,
    .connect = touchpad_gest_connect,
    .disconnect = touchpad_gest_disconnect,
    .name = DEVICE_NAME,
    .id_table = touchpad_gest_ids,
};

static struct proc_ops touchpad_gest_fops = {.proc_write = proc_write,
                                             .proc_read = proc_read};

static int __init touchpad_gest_init(void) {
  int error;

  error = input_register_handler(&touchpad_gest_handler);

  if (error) {
    printk(KERN_ERR
           "touchpad_gestures.c: registering input handler failed with (%d)\n",
           error);
  } else {
    printk(KERN_INFO "touchpad_gestures.c: handler registerd");
  }

  proc_file =
      proc_create("touchpad_gest_proc_file", 0666, NULL, &touchpad_gest_fops);
  if (!proc_file) {
    printk("touchpad_gestures.c: couldn't create proc_file");
    return -ENOMEM;
  }

  INIT_WORK(&my_work.work, parse_pattern);
  printk("touchpad_gestures.c: insmod complete");
  return 0;

  return error;
}

static void __exit touchpad_gest_exit(void) {
  input_unregister_handler(&touchpad_gest_handler);
  remove_proc_entry("touchpad_gest_proc_file", NULL);
  printk("touchpad_gestures.c: rmmod complete");
}

module_init(touchpad_gest_init);
module_exit(touchpad_gest_exit);
