/*
 * TriniOS ext4 Filesystem Driver
 *
 * On-disk structures, constants, and type definitions for ext4.
 * Clean-room implementation based on the ext4 specification.
 *
 * Reference: https://www.kernel.org/doc/html/latest/filesystems/ext4/
 */

#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * ext4 Constants
 * ============================================================================ */

#define EXT4_SUPERBLOCK_MAGIC        0xEF53
#define EXT4_GOOD_MAGIC              0xEF53

#define EXT4_OS_LINUX                0
#define EXT4_OS_HURD                 1
#define EXT4_OS_MASIX                2

#define EXT4_ERR_NONE                0
#define EXT4_ERR_CONTINUE            1
#define EXT4_ERR_PANIC              2
#define EXT4_ERR_REPEAT             3

/* Compatible feature flags */
#define EXT4_FEATURE_COMPAT_DIR_NLINK        0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES    0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL      0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR         0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE     0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX        0x0020
#define EXT4_FEATURE_COMPAT_LSTIME           0x0040
#define EXT4_FEATURE_COMPAT_PROJECT          0x0080

/* Incompatible feature flags */
#define EXT4_FEATURE_INCOMPAT_FILETYPE       0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER        0x0004
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV    0x0008
#define EXT4_FEATURE_INCOMPAT_META_BG        0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENT         0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT          0x0080
#define EXT4_FEATURE_INCOMPAT_FLEX_BG        0x0100
#define EXT4_FEATURE_INCOMPAT_EA_INODE       0x0200
#define EXT4_FEATURE_INCOMPAT_DIRDATA        0x0400

/* Read-only compatible feature flags */
#define EXT4_FEATURE_RO_COMPAT_sparse_super  0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE    0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR     0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE     0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM      0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK     0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE   0x0040

/* Inode types */
#define EXT4_FT_UNKNOWN          0
#define EXT4_FT_REG_FILE         1
#define EXT4_FT_DIR              2
#define EXT4_FT_CHARDEV          3
#define EXT4_FT_BLOCKDEV         4
#define EXT4_FT_FIFO             5
#define EXT4_FT_SOCKET           6
#define EXT4_FT_SYMLINK          7

/* Block group descriptor table flags */
#define EXT4_BG_INODE_UNINIT             0x0001
#define EXT4_BG_BLOCK_UNINIT             0x0002
#define EXT4_BG_INODE_UNINIT             0x0004

/* Inode flags */
#define EXT4_SECRM_FL                  0x00000001
#define EXT4_UNRM_FL                   0x00000002
#define EXT4_COMPR_FL                  0x00000004
#define EXT4_SYNC_FL                   0x00000008
#define EXT4_IMMUTABLE_FL              0x00000010
#define EXT4_APPEND_FL                 0x00000020
#define EXT4_NODUMP_FL                 0x00000040
#define EXT4_NOATIME_FL                0x00000080
#define EXT4_DIRTY_FL                  0x00000100
#define EXT4_HUGE_FILE_FL              0x00004000
#define EXT4_EXTENTS_FL                0x00080000
#define EXT4_EA_INODE_FL               0x00400000
#define EXT4_EOFBLOCKS_FL              0x01000000

/* File mode macros */
#define EXT4_S_IFMT        0xF000
#define EXT4_S_IFDIR       0x4000
#define EXT4_S_IFREG       0x8000
#define EXT4_S_IFLNK       0xA000
#define EXT4_S_IFBLK       0x6000
#define EXT4_S_IFCHR       0x2000
#define EXT4_S_IFIFO       0x1000
#define EXT4_S_IFSOCK      0xC000

#define EXT4_S_ISDIR(m)    (((m) & EXT4_S_IFMT) == EXT4_S_IFDIR)
#define EXT4_S_ISREG(m)    (((m) & EXT4_S_IFMT) == EXT4_S_IFREG)
#define EXT4_S_ISLNK(m)    (((m) & EXT4_S_IFMT) == EXT4_S_IFLNK)
#define EXT4_S_ISBLK(m)    (((m) & EXT4_S_IFMT) == EXT4_S_IFBLK)
#define EXT4_S_ISCHR(m)    (((m) & EXT4_S_IFMT) == EXT4_S_IFCHR)
#define EXT4_S_ISFIFO(m)   (((m) & EXT4_S_IFMT) == EXT4_S_IFIFO)
#define EXT4_S_ISSOCK(m)   (((m) & EXT4_S_IFMT) == EXT4_S_IFSOCK)

/* Directory entry types */
#define EXT4_DE_UNKNOWN          0
#define EXT4_DE_REG_FILE         1
#define EXT4_DE_DIR              2
#define EXT4_DE_CHRDEV           3
#define EXT4_DE_BLKDEV           4
#define EXT4_DE_FIFO             5
#define EXT4_DE_SOCK             6
#define EXT4_DE_LNK              7

/* Journal magic values */
#define EXT4_JBD2_HEADER_MAGIC           'J'
#define EXT4_JBD2_MAGIC_BARRIER          0x0802
#define EXT4_JBD2_MAGIC_DATA             0x0803
#define EXT4_JBD2_MAGIC_DESCRIPTOR       0x0804
#define EXT4_JBD2_MAGIC_REVOKE           0x0805
#define EXT4_JBD2_MAGIC_REVOKE_RECORD    0x0806
#define EXT4_JBD2_MAGIC_COMMIT           0x0807
#define EXT4_JBD2_MAGIC_ABORT            0x0808
#define EXT4_JBD2_MAGIC_REVOKE_HEADER    0x0809
#define EXT4_JBD2_MAGIC_CHECKPOINT_DELETE 0x080A
#define EXT4_JBD2_MAGIC_CHECKPOINT_BLOCK  0x080B
#define EXT4_JBD2_MAGIC_UUID             0x080C
#define EXT4_JBD2_MAGIC_FREE             0x080D
#define EXT4_JBD2_MAGIC_MEMORY_BARRIER   0x080E

/* Journal states */
#define EXT4_JOURNAL_STATE_IDLE  0
#define EXT4_JOURNAL_STATE_BUSY  1
#define EXT4_JOURNAL_STATE_ERROR 2

/* Filesystem states */
#define EXT4_VALID_FS            0x0001
#define EXT4_ERROR_FS            0x0002
#define EXT4_ORPHAN_FS           0x0004

/* Alignment helpers */
#define EXT4_CROUND(x) (((x) + 3) & ~3)

/* Default values */
#define EXT4_DEFAULT_INODE_SIZE      128
#define EXT4_MIN_BLOCK_LOG_SIZE      2
#define EXT4_MAX_BLOCK_LOG_SIZE      4
#define EXT4_MIN_BLOCK_SIZE          (1024 << EXT4_MIN_BLOCK_LOG_SIZE)
#define EXT4_MAX_BLOCK_SIZE          (1024 << EXT4_MAX_BLOCK_LOG_SIZE)
#define EXT4_ADDR_PER_BLOCK          15
#define EXT4_MAX_BLOCKS_PER_FILE     (16 * 1024 * 1024 * 1024)
#define EXT4_MAX_DIR_SIZE            (2 * 1024 * 1024 * 1024)
#define EXT4_PRIV_DATA_BLOCK_SIZE    1024

/* Reserved inode numbers */
#define EXT4_GOOD_OLD_FIRST_INO      11
#define EXT4_JOURNAL_INODE           8

/* ============================================================================
 * ext4 On-Disk Structures
 * ============================================================================ */

/* --- Superblock (1024 bytes, at offset 1024 = block 1) --- */
typedef struct {
    uint32_t  s_inodes_count;
    uint32_t  s_blocks_count_lo;
    uint32_t  s_r_blocks_count_lo;
    uint32_t  s_free_blocks_count_lo;
    uint32_t  s_free_inodes_count;
    uint32_t  s_first_data_block;
    uint32_t  s_log_block_size;
    uint32_t  s_log_cluster_size;
    uint32_t  s_blocks_per_group;
    uint32_t  s_clusters_per_group;
    uint32_t  s_inodes_per_group;
    uint32_t  s_mtime;
    uint32_t  s_wtime;
    uint16_t  s_mnt_count;
    int16_t   s_max_mnt_count;
    uint16_t  s_magic;
    uint16_t  s_state;
    uint16_t  s_errors;
    uint16_t  s_minor_rev_level;
    uint32_t  s_lastcheck;
    uint32_t  s_checkinterval;
    uint32_t  s_creator_os;
    uint32_t  s_rev_level;
    uint16_t  s_def_resuid;
    uint16_t  s_def_resgid;
    uint32_t  s_first_ino;
    uint16_t  s_inode_size;
    uint16_t  s_block_group_nr;
    uint32_t  s_feature_compat;
    uint32_t  s_feature_incompat;
    uint32_t  s_feature_ro_compat;
    uint8_t   s_uuid[16];
    char      s_volume_name[16];
    char      s_last_mounted[64];
    uint32_t  s_algorithm_usage_bitmap;
    uint8_t   s_prealloc_blocks;
    uint8_t   s_prealloc_dir_blocks;
    uint16_t  s_reserved_gdt_blocks;
    uint8_t   s_journal_uuid[16];
    uint32_t  s_journal_inum;
    uint32_t  s_journal_dev;
    uint32_t  s_last_orphan;
    uint32_t  s_hash_seed[4];
    uint8_t   s_def_hash_version;
    uint8_t   s_jnl_backup_type;
    uint32_t  s_desc_size;
    uint32_t  s_default_mount_opts;
    uint32_t  s_first_meta_bg;
    uint32_t  s_mkfs_time;
    uint32_t  s_jnl_blocks[17];
    uint32_t  s_blocks_count_hi;
    uint32_t  s_r_blocks_count_hi;
    uint32_t  s_free_blocks_count_hi;
    uint16_t  s_min_extra_isize;
    uint16_t  s_want_extra_isize;
    uint16_t  s_flags;
    uint8_t   s_raid_stride;
    uint8_t   s_mmp_interval;
    uint64_t  s_mmp_block;
    uint32_t  s_raid_stripe_width;
    uint32_t  s_log_groups_per_flex;
    uint32_t  s_kbytes_written;
    uint8_t   s_encryption_level;
    uint8_t   s_encryption_alg;
    uint8_t   s_reserved[606];
} __attribute__((packed)) ext4_superblock_t;

/* --- Group Descriptor (64 bytes with 64-bit features) --- */
typedef struct {
    uint32_t  bg_block_bitmap_lo;
    uint32_t  bg_inode_bitmap_lo;
    uint32_t  bg_inode_table_lo;
    uint16_t  bg_free_blocks_count_lo;
    uint16_t  bg_free_inodes_count_lo;
    uint16_t  bg_used_dirs_count_lo;
    uint16_t  bg_flags;
    uint32_t  bg_exclude_bitmap_lo;
    uint16_t  bg_block_bitmap_csum_lo;
    uint16_t  bg_inode_bitmap_csum_lo;
    uint16_t  bg_itable_unused_lo;
    uint16_t  bg_checksum;
    uint32_t  bg_block_bitmap_hi;
    uint32_t  bg_inode_bitmap_hi;
    uint32_t  bg_inode_table_hi;
    uint16_t  bg_free_blocks_count_hi;
    uint16_t  bg_free_inodes_count_hi;
    uint16_t  bg_used_dirs_count_hi;
    uint16_t  bg_itable_unused_hi;
    uint32_t  bg_exclude_bitmap_hi;
    uint16_t  bg_block_bitmap_csum_hi;
    uint16_t  bg_inode_bitmap_csum_hi;
    uint16_t  bg_reserved;
} __attribute__((packed)) ext4_group_desc_t;

/* --- Inode (256 bytes max with extra_isize) --- */
typedef struct {
    uint16_t  i_mode;
    uint16_t  i_uid_lo;
    uint32_t  i_size_lo;
    uint32_t  i_atime;
    uint32_t  i_ctime;
    uint32_t  i_mtime;
    uint32_t  i_dtime;
    uint16_t  i_gid_lo;
    uint16_t  i_links_count;
    uint32_t  i_blocks_lo;
    uint32_t  i_flags;
    uint32_t  i_osd1;
    uint32_t  i_block[15];
    uint32_t  i_generation;
    uint32_t  i_file_acl_lo;
    uint32_t  i_size_hi;
    uint32_t  i_obso_faddr;
    uint16_t  i_osd2;
    uint16_t  i_extra_isize;
    uint16_t  i_uid_hi;
    uint16_t  i_gid_hi;
    uint16_t  i_checksum_lo;
    uint16_t  i_reserved;
    uint32_t  i_ctime_extra;
    uint32_t  i_mtime_extra;
    uint32_t  i_atime_extra;
    uint32_t  i_crtime;
    uint32_t  i_crtime_extra;
    uint32_t  i_version_hi;
    uint16_t  i_projid_lo;
    uint16_t  i_projid_hi;
    uint32_t  i_reserved2;
    uint32_t  i_file_acl_hi;
    uint32_t  i_blocks_hi;
    uint32_t  i_padding;
    uint16_t  i_checksum_hi;
    uint16_t  i_kbytes_hi;
    uint32_t  i_dir_acl;
    uint32_t  i_file_acl[4];
    uint32_t  i_dir_acl[4];
    uint64_t  i_file_acl[2];
    uint64_t  i_dir_acl[2];
    uint32_t  i_reserved[4];
} __attribute__((packed)) ext4_inode_t;

/* --- Directory Entry (variable size) --- */
typedef struct {
    uint32_t  inode;
    uint16_t  rec_len;
    uint8_t   name_len;
    uint8_t   file_type;
    char      name[1];
} __attribute__((packed)) ext4_dir_entry_2_t;

/* --- Extent Format --- */
#define EXT4_EXTENT_MAGIC          0xF30A
#define EXT4_EXTENT_MAX_DEPTH      4
#define EXT4_EXTENT_MAX_NODES      ((1024 - 12) / 12)
#define EXT4_EXTENT_MAX_LEAVES     ((1024 - 12) / 16)

typedef struct {
    uint16_t  eh_magic;
    uint16_t  eh_max;
    uint16_t  eh_depth;
    uint16_t  eh_generation;
    uint32_t  eh_root;
} __attribute__((packed)) ext4_extent_header_t;

typedef struct {
    uint16_t  ee_block;
    uint16_t  ee_len;
    uint16_t  ee_start_hi;
    uint32_t  ee_start_lo;
} __attribute__((packed)) ext4_extent_t;

typedef struct {
    uint16_t  ei_block;
    uint16_t  ei_leaf_depth;
    uint16_t  ei_branch;
    uint32_t  ei_start;
} __attribute__((packed)) ext4_extent_index_t;

/* --- Journal Superblock --- */
typedef struct {
    uint32_t  s_blocksize;
    uint32_t  s_maxlen;
    uint32_t  s_first;
    uint32_t  s_sequence;
    uint32_t  s_error;
    uint32_t  s_magic;
    uint32_t  s_blocktype;
    uint32_t  s_nr_usage;
    uint32_t  s_first_usage;
    uint32_t  s_max_usage;
    uint32_t  s_optional_stats;
    uint8_t   s_uuid[16];
    uint32_t  s_checksum;
} __attribute__((packed)) ext4_journal_superblock_t;

/* --- Journal Block Header --- */
typedef struct {
    uint8_t   h_magic;
    uint8_t   h_blocktype;
    uint16_t  h_sequence;
    uint32_t  h_flags;
} __attribute__((packed)) ext4_journal_header_t;

/* --- Journal Descriptor --- */
typedef struct {
    ext4_journal_header_t jh;
    uint32_t              dl_block[0];
} __attribute__((packed)) ext4_journal_descriptor_t;

/* --- Journal Commit --- */
typedef struct {
    ext4_journal_header_t jh;
    uint64_t              c_commit_number;
    uint32_t              c_commit_sequence;
    uint32_t              c_checksum;
} __attribute__((packed)) ext4_journal_commit_t;

/* ============================================================================
 * Runtime Structures (in-memory only)
 * ============================================================================ */

/* Block device operations */
typedef struct {
    void           *dev;
    int            (*read_blocks)(void *dev, uint64_t block, uint32_t count, void *buf);
    int            (*write_blocks)(void *dev, uint64_t block, uint32_t count, const void *buf);
    uint64_t       total_blocks;
    uint32_t       block_size;
} ext4_block_device_t;

/* Mounted filesystem state */
typedef struct {
    ext4_superblock_t     sb;
    ext4_group_desc_t    *group_desc;
    ext4_block_device_t  *bdev;
    uint32_t              block_size;
    uint32_t              block_size_log2;
    uint32_t              blocks_count;
    uint32_t              groups_count;
    uint32_t              inode_size;
    bool                  has_journal;
    bool                  has_extents;
    bool                  is_64bit;
    /* Journal state */
    uint32_t              journal_ino;
    uint32_t              journal_dev;
    /* Caches */
    uint8_t              *block_cache;
    uint64_t              block_cache_addr;
    uint32_t              block_cache_count;
} ext4_fs_t;

/* Open file handle */
typedef struct {
    uint32_t    ino;
    uint64_t    pos;
    uint16_t    flags;
    uint8_t     mode;
    bool        is_dir;
} ext4_file_t;

/* Directory read state */
typedef struct {
    uint32_t    dir_ino;
    uint64_t    block_pos;
    uint8_t    *block_buf;
    uint16_t    offset_in_block;
} ext4_dirhandle_t;

/* Directory entry result */
typedef struct {
    uint32_t    ino;
    uint8_t     file_type;
    char        name[256];
} ext4_dirent_t;

/* File statistics */
typedef struct {
    uint32_t    ino;
    uint16_t    mode;
    uint32_t    nlinks;
    uint32_t    uid;
    uint32_t    gid;
    uint64_t    size;
    uint64_t    blocks;
    uint32_t    atime;
    uint32_t    mtime;
    uint32_t    ctime;
} ext4_stat_t;

/* ============================================================================
 * Error Codes
 * ============================================================================ */

#define EXT4_OK                  0
#define EXT4_ERR_INVAL           -1
#define EXT4_ERR_IO              -2
#define EXT4_ERR_NOENT           -3
#define EXT4_ERR_NOTDIR          -4
#define EXT4_ERR_ISDIR           -5
#define EXT4_ERR_EXIST           -6
#define EXT4_ERR_NAMETOOLONG     -7
#define EXT4_ERR_NODEV           -8
#define EXT4_ERR_BADF            -9
#define EXT4_ERR_FBIG            -10
#define EXT4_ERR_NOSPC           -11
#define EXT4_ERR_MLINK           -12
#define EXT4_ERR_ROFS            -13
#define EXT4_ERR_OVERFLOW        -14
#define EXT4_ERR_OPNOTSUPP       -15
#define EXT4_ERR_PERM            -16
#define EXT4_ERR_ACCES           -17
#define EXT4_ERR_NOTSUPP         -18

/* ============================================================================
 * API: Block Device Layer
 * ============================================================================ */

int  ext4_bdev_init(ext4_block_device_t *bdev, void *dev, uint64_t total_blocks, uint32_t block_size);
int  ext4_bdev_read(ext4_block_device_t *bdev, uint64_t block, uint32_t count, void *buf);
int  ext4_bdev_write(ext4_block_device_t *bdev, uint64_t block, uint32_t count, const void *buf);

/* ============================================================================
 * API: Mount / Unmount
 * ============================================================================ */

int  ext4_mount(ext4_fs_t *fs, ext4_block_device_t *bdev);
void ext4_unmount(ext4_fs_t *fs);

/* ============================================================================
 * API: File Operations
 * ============================================================================ */

int  ext4_open(ext4_fs_t *fs, const char *path, uint16_t flags, ext4_file_t *file);
int  ext4_close(ext4_fs_t *fs, ext4_file_t *file);
int  ext4_read(ext4_fs_t *fs, ext4_file_t *file, void *buf, uint64_t count, uint64_t *done);
int  ext4_write(ext4_fs_t *fs, ext4_file_t *file, const void *buf, uint64_t count, uint64_t *done);
int  ext4_lseek(ext4_fs_t *fs, ext4_file_t *file, int64_t offset, int whence, uint64_t *new_pos);
int  ext4_stat(ext4_fs_t *fs, const char *path, ext4_stat_t *st);
int  ext4_unlink(ext4_fs_t *fs, const char *path);
int  ext4_rename(ext4_fs_t *fs, const char *old_path, const char *new_path);

/* ============================================================================
 * API: Directory Operations
 * ============================================================================ */

int  ext4_opendir(ext4_fs_t *fs, const char *path, ext4_dirhandle_t *dh);
int  ext4_readdir(ext4_fs_t *fs, ext4_dirhandle_t *dh, ext4_dirent_t *entry);
int  ext4_closedir(ext4_dirhandle_t *dh);
int  ext4_mkdir(ext4_fs_t *fs, const char *path, uint16_t mode);
int  ext4_rmdir(ext4_fs_t *fs, const char *path);

/* ============================================================================
 * API: Inode Operations
 * ============================================================================ */

int  ext4_iget(ext4_fs_t *fs, uint32_t ino, ext4_inode_t *inode);
int  ext4_inode_data_block(ext4_fs_t *fs, ext4_inode_t *inode, uint64_t lblock, uint64_t *pblock);
int  ext4_path_lookup(ext4_fs_t *fs, const char *path, uint32_t *ino, char *last_component);

/* ============================================================================
 * API: Journal
 * ============================================================================ */

int  ext4_journal_check(ext4_fs_t *fs);
int  ext4_journal_needs_recovery(ext4_fs_t *fs, bool *needs_recovery);

/* ============================================================================
 * API: Debug / Info
 * ============================================================================ */

void ext4_dump_superblock(ext4_fs_t *fs);
void ext4_dump_inode(ext4_fs_t *fs, ext4_inode_t *inode);
int  ext4_fsck_light(ext4_fs_t *fs);

#endif /* EXT4_H */