#ifndef VTFS_H
#define VTFS_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define MODULE_NAME "vtfs"
#define VTFS_ROOT_INO 101

#define LOG(fmt, ...) pr_info("[" MODULE_NAME "]: " fmt, ##__VA_ARGS__)

struct file_data {
    char *raw_data;
    size_t size;
    size_t buff_size;
};

struct vtfs_file_info {
    char name[256];
    ino_t ino;
    ino_t parent_ino;
    bool is_dir;
    struct list_head list;
    struct mutex lock;
    struct file_data content;
};


// монтирование
struct dentry* vtfs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data);
void vtfs_kill_sb(struct super_block* sb);
int vtfs_fill_super(struct super_block *sb, void *data, int silent);

// inode
struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino);
struct dentry* vtfs_lookup(struct inode* parent_inode, struct dentry* child_dentry, unsigned int flag);
int vtfs_iterate(struct file* filp, struct dir_context* ctx);

// работа с файлами
int vtfs_create(struct mnt_idmap *idmap, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode, bool b);
int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry);
struct dentry *vtfs_mkdir(struct mnt_idmap *idmap, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode);
int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry);

// read & write
ssize_t vtfs_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset);
ssize_t vtfs_write(struct file *filp, const char __user *buffer, size_t length, loff_t *offset);

// utils
struct vtfs_file_info *get_file_by_inode(ino_t ino);
struct vtfs_file_info *find_file_in_dir(const char *name, ino_t parent_ino);

#endif
