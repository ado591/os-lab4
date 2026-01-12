#include "vtfs.h"
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/namei.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Angela Obraztsova P3322");
MODULE_DESCRIPTION("A simple FS kernel module");


static LIST_HEAD(vtfs_files);
static int next_ino = 103; // 101 и 102 заняты
static DEFINE_MUTEX(vtfs_files_lock);


struct file_system_type vtfs_fs_type = {
    .name = "vtfs",
    .mount = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

struct inode_operations vtfs_inode_ops = {
 .lookup = vtfs_lookup,
 .create = vtfs_create,
 .unlink = vtfs_unlink,
 .mkdir = vtfs_mkdir,
 .rmdir = vtfs_rmdir,
};

struct file_operations vtfs_dir_ops = {
 .iterate = vtfs_iterate,
};

struct file_operations vtfs_file_ops = {
    .open = simple_open,
    .read = vtfs_read,
    .write = vtfs_write,
};

static int __init vtfs_init(void) {
    int code = register_filesystem(&vtfs_fs_type);
    if (code) { 
        LOG("FATAL: cannot register filesystem\n");
    } else {
      LOG("VTFS joined the kernel\n");
    }
    return code;
}

static void __exit vtfs_exit(void) {
    unregister_filesystem(&vtfs_fs_type);
    LOG("VTFS left the kernel\n");
}

struct dentry* vtfs_mount(struct file_system_type* fs_type, int flags, const char* token, void* data) {
    struct dentry* ret = mount_nodev(fs_type, flags, data, vtfs_fill_super);
    if (ret == NULL) {
        printk(KERN_ERR "Can't mount file system\n");
    } else {
        printk(KERN_INFO "Mounted successfully\n");
    }
    return ret;
}

int vtfs_fill_super(struct super_block *sb, void *data, int silent) {
    struct inode* inode = vtfs_get_inode(sb, NULL, S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO, 100);
    if (inode == NULL) {
        return -ENOMEM;
    }

    inode->i_op = &vtfs_inode_ops;
    inode->i_fop = &vtfs_dir_ops;
    sb->s_root = d_make_root(inode);
    if (sb->s_root == NULL) {
        return -ENOMEM;
    }
    printk(KERN_INFO "return 0\n"); //я честно так и не поняла, зачем return 0 в dmesg...
    return 0;
}

struct inode* vtfs_get_inode(struct super_block* sb, const struct inode* dir, umode_t mode, int i_ino) {
    struct inode *inode = new_inode(sb);
    if (inode != NULL) {
        inode->i_mode = mode;
        i_uid_write(inode, 0);
        i_gid_write(inode, 0);
        inode->i_ino = i_ino;
        
        inode_set_mtime_to_ts(inode, current_time(inode));
        inode_set_atime_to_ts(inode, current_time(inode));
        inode_set_ctime_to_ts(inode, current_time(inode));

    }
    LOG("get_inode отработал");
    return inode;
}

void vtfs_kill_sb(struct super_block* sb) {
    struct vtfs_file_info *file_info, *tmp;
    
    list_for_each_entry_safe(file_info, tmp, &vtfs_files, list) {
        if (file_info->content.raw_data) {
            kfree(file_info->content.raw_data);
        }
        list_del(&file_info->list);
        kfree(file_info);
    }
    
    kill_litter_super(sb);
    printk(KERN_INFO "vtfs super block is destroyed. Unmount successfully.\n");
}

struct dentry *vtfs_lookup(struct inode *parent_inode,
                           struct dentry *child_dentry,
                           unsigned int flag) {
    const char *name = child_dentry->d_name.name;
    struct vtfs_file_info *file_info;

    LOG("Начинаем лук вверх\n");
    file_info = find_file_in_dir(name, parent_inode->i_ino);
    if (file_info) {
        struct inode *inode = vtfs_get_inode(
            parent_inode->i_sb,
            NULL,
            file_info->is_dir ? S_IFDIR : S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO,
            file_info->ino
        );

        if (inode) {
            inode->i_op = &vtfs_inode_ops;
            inode->i_fop = file_info->is_dir ? &vtfs_dir_ops : &vtfs_file_ops;
            d_add(child_dentry, inode);
        }
    }
    return NULL;
}

int vtfs_iterate(struct file *filp, struct dir_context *ctx) {
    struct dentry *dentry = filp->f_path.dentry;
    struct inode *inode = dentry->d_inode;
    ino_t current_ino = inode->i_ino;
    int pos = ctx->pos;
    
    if (pos < 0)
        return 0;

    if (pos == 0) {
        if (!dir_emit(ctx, ".", 1, current_ino, DT_DIR))
            return 0;
        ctx->pos++;
        return 1;
    }

    if (pos == 1) {
        struct vtfs_file_info *current_dir = get_file_by_inode(current_ino);
        ino_t parent_ino = current_dir ? current_dir->parent_ino : VTFS_ROOT_INO;
        if (!dir_emit(ctx, "..", 2, parent_ino, DT_DIR))
            return 0;
        ctx->pos++;
        return 1;
    }

    struct vtfs_file_info *file_info;
    int count = 2;
    
    list_for_each_entry(file_info, &vtfs_files, list) {
        if (file_info->parent_ino == current_ino) {
            if (count == pos) {
                unsigned char type = file_info->is_dir ? DT_DIR : DT_REG;
                if (!dir_emit(ctx, file_info->name, strlen(file_info->name),
                            file_info->ino, type))
                    return 0;
                ctx->pos++;
                return 1;
            }
            count++;
        }
    }
    
    return 0;
}

int vtfs_create(struct mnt_idmap *idmap, struct inode *parent_inode, 
                struct dentry *child_dentry, umode_t mode, bool b) {
    struct vtfs_file_info *new_file_info = kmalloc(sizeof(*new_file_info), GFP_KERNEL);
    if (!new_file_info) {
        return -ENOMEM;
    }

    new_file_info->ino = next_ino++;
    new_file_info->content.raw_data = NULL;
    new_file_info->content.size = 0;
    new_file_info->content.buff_size = 0;
    new_file_info->is_dir = false;
    new_file_info->parent_ino = parent_inode->i_ino;
    mutex_init(&new_file_info->lock);
    
    snprintf(new_file_info->name, sizeof(new_file_info->name), "%s", child_dentry->d_name.name);
    
    list_add(&new_file_info->list, &vtfs_files);
    
    struct inode *inode = vtfs_get_inode(parent_inode->i_sb, NULL, 
                                       S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO,
                                       new_file_info->ino);
    if (!inode) {
        list_del(&new_file_info->list);
        kfree(new_file_info);
        return -ENOMEM;
    }

    inode->i_op = &vtfs_inode_ops;
    inode->i_fop = &vtfs_file_ops;
    d_add(child_dentry, inode);

    LOG("create отработал");
    
    return 0;
}

int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry) {
    const char *name = child_dentry->d_name.name;
    struct vtfs_file_info *file_info, *tmp;
    
    list_for_each_entry_safe(file_info, tmp, &vtfs_files, list) {
        if (!strcmp(name, file_info->name)) {
            if (file_info->content.raw_data) {
                kfree(file_info->content.raw_data);
            }
            list_del(&file_info->list);
            kfree(file_info);
            break;
        }
    }
    
    LOG("unlink отработал");
    return simple_unlink(parent_inode, child_dentry);
}

//в задании была сигнатура с int, 
// но на виртуалке linux 6.27.0-5-generic
// у него сигнатура другая https://github.com/torvalds/linux/blob/master/include/linux/fs.h#L2000
struct dentry *vtfs_mkdir(struct mnt_idmap *idmap, struct inode *parent_inode,
               struct dentry *child_dentry, umode_t mode) {
    struct inode *inode;
    ino_t parent_ino = parent_inode->i_ino;
    const char *name = child_dentry->d_name.name;
    
    if (find_file_in_dir(name, parent_ino))
        return ERR_PTR(-EEXIST);
    
    mutex_lock(&vtfs_files_lock);
    
    inode = vtfs_get_inode(parent_inode->i_sb, NULL,
                          S_IFDIR | mode, next_ino++);
    
    if (!inode) {
        mutex_unlock(&vtfs_files_lock);
        return ERR_PTR(-ENOMEM);
    }
    
    struct vtfs_file_info *dir_info = kmalloc(sizeof(*dir_info), GFP_KERNEL);
    if (!dir_info) {
        iput(inode);
        mutex_unlock(&vtfs_files_lock);
        return ERR_PTR(-ENOMEM);
    }
    
    // TODO: информацию о директории в отдельную функцию??
    memset(dir_info, 0, sizeof(*dir_info));
    strncpy(dir_info->name, name, 255);
    dir_info->name[255] = '\0';
    dir_info->ino = inode->i_ino;
    dir_info->parent_ino = parent_ino;
    dir_info->is_dir = true;
    mutex_init(&dir_info->lock);
    
    list_add(&dir_info->list, &vtfs_files);
    
    inode->i_op = &vtfs_inode_ops;
    inode->i_fop = &vtfs_dir_ops;
    d_add(child_dentry, inode);
    
    mutex_unlock(&vtfs_files_lock);
    LOG("mkdir отработал");
    return NULL;
}

int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry) {
    const char *name = child_dentry->d_name.name;
    struct vtfs_file_info *dir_info, *tmp;
    struct inode *dir_inode = d_inode(child_dentry);
    
    if (!simple_empty_dir(child_dentry))
        return -ENOTEMPTY;
    
    list_for_each_entry_safe(dir_info, tmp, &vtfs_files, list) {
        if (!strcmp(name, dir_info->name) && dir_info->ino == dir_inode->i_ino) {
            list_del(&dir_info->list);
            kfree(dir_info);
            break;
        }
    }

    LOG("rmdir отработал");
    return simple_rmdir(parent_inode, child_dentry);
}

struct vtfs_file_info *get_file_by_inode(ino_t ino) {
    struct vtfs_file_info *file_info;
    
    list_for_each_entry(file_info, &vtfs_files, list) {
        if (file_info->ino == ino) {
            return file_info;
        }
    }
    return NULL;
}

struct vtfs_file_info *find_file_in_dir(const char *name, ino_t parent_ino) {
    struct vtfs_file_info *file_info;

    list_for_each_entry(file_info, &vtfs_files, list) {
        if (file_info->parent_ino == parent_ino && strcmp(file_info->name, name) == 0) {
            return file_info;
        }
    }
    return NULL;
}

ssize_t vtfs_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset) {
    struct inode *inode = filp->f_inode;
    struct vtfs_file_info *file_info = get_file_by_inode(inode->i_ino);

    if (!file_info) {
        return -ENOENT;
    }

    if (!file_info->content.raw_data && file_info->content.buff_size > 0) {
        mutex_unlock(&file_info->lock);
        return -EIO;
    }

    mutex_lock(&file_info->lock);
    
    if (*offset >= file_info->content.size) {
        mutex_unlock(&file_info->lock);
        return 0;
    }

    length = min(length, (size_t)(file_info->content.size - *offset));

    if (copy_to_user(buffer, file_info->content.raw_data + *offset, length)) {
        mutex_unlock(&file_info->lock);
        return -EFAULT;
    }

    *offset += length;
    
    mutex_unlock(&file_info->lock);
    return length;
}

static int is_ascii_string(const char *buffer, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)buffer[i] > 127) {
            return -EINVAL;
        }
    }
    return 0;
}

ssize_t vtfs_write(struct file *filp, const char __user *buffer, size_t length, loff_t *offset) {
    struct inode *inode = filp->f_inode;
    struct vtfs_file_info *file_info = get_file_by_inode(inode->i_ino);

    if (!file_info) {
        return -ENOENT;
    }

    if (!file_info->content.raw_data && file_info->content.buff_size > 0) {
        mutex_unlock(&file_info->lock);
        return -EIO;
    }

    mutex_lock(&file_info->lock);

    if (*offset == 0) {
        file_info->content.size = 0;
        if (file_info->content.raw_data) {
            memset(file_info->content.raw_data, 0, file_info->content.buff_size);
        }
    }

    size_t required_size = *offset + length;
    
    if (required_size > file_info->content.buff_size) {
        size_t new_size = max(required_size, file_info->content.buff_size * 2);
        if (new_size == 0) 
            new_size = PAGE_SIZE;
        
        char *new_data = krealloc(file_info->content.raw_data, new_size, GFP_KERNEL);
        if (!new_data) {
            mutex_unlock(&file_info->lock);
            return -ENOMEM;
        }
        
        if (new_size > file_info->content.buff_size) {
            memset(new_data + file_info->content.buff_size, 0, 
                   new_size - file_info->content.buff_size);
        }
        
        file_info->content.raw_data = new_data;
        file_info->content.buff_size = new_size;
    }

    char *tmp_buffer = kmalloc(length, GFP_KERNEL);
    if (!tmp_buffer) {
        mutex_unlock(&file_info->lock);
        return -ENOMEM;
    }

    if (copy_from_user(tmp_buffer, buffer, length)) {
        kfree(tmp_buffer);
        mutex_unlock(&file_info->lock);
        return -EFAULT;
    }

    if (is_ascii_string(tmp_buffer, length) != 0) {
        kfree(tmp_buffer);
        mutex_unlock(&file_info->lock);
        return -EINVAL;
    }

    memcpy(file_info->content.raw_data + *offset, tmp_buffer, length);
    kfree(tmp_buffer);

    if (required_size > file_info->content.size) {
        file_info->content.size = required_size;
    }

    *offset += length;
    
    inode_set_mtime_to_ts(inode, current_time(inode)); //время модификации надо сменить
    mutex_unlock(&file_info->lock);
    return length;
}


module_init(vtfs_init);
module_exit(vtfs_exit);
