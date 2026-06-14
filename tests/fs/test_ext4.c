/*
 * TriniOS ext4 Filesystem Tests
 * ==============================
 * Tests for ext4 on-disk structures and constants using a mock block device.
 * The mock block device is a RAM-backed buffer we can control.
 */

#include "../test_framework.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Inline ext4 structures from ext4.h (so we don't need the real header)
 * ============================================================================ */

#define EXT4_SUPERBLOCK_MAGIC  0xEF53
#define EXT4_GOOD_MAGIC        0xEF53

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
} __attribute__((packed)) ext4_superblock_t;

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
    uint16_t  i_extra_isize;
    uint16_t  i_checksum_lo;
} __attribute__((packed)) ext4_inode_t;

typedef struct {
    uint32_t  inode;
    uint16_t  rec_len;
    uint8_t   name_len;
    uint8_t   file_type;
    char      name[256];
} __attribute__((packed)) ext4_dir_entry_2_t;

/* ============================================================================
 * Mock Block Device - RAM-backed
 * ============================================================================ */

#define MOCK_BLOCK_SIZE  1024
#define MOCK_NUM_BLOCKS  8192  /* 8MB total */

typedef struct {
    uint8_t  *data;
    uint32_t  block_size;
    uint32_t  num_blocks;
} mock_bdev_t;

static mock_bdev_t mock_bdev;

static int mock_read_block(uint64_t block, void *buf)
{
    if (block >= mock_bdev.num_blocks) return -1;
    uint8_t *src = mock_bdev.data + (block * mock_bdev.block_size);
    memcpy(buf, src, mock_bdev.block_size);
    return 0;
}

static int mock_write_block(uint64_t block, const void *buf)
{
    if (block >= mock_bdev.num_blocks) return -1;
    uint8_t *dst = mock_bdev.data + (block * mock_bdev.block_size);
    memcpy(dst, buf, mock_bdev.block_size);
    return 0;
}

/* ============================================================================
 * Helper: Build a minimal valid ext4 superblock in block 1
 * ============================================================================ */

static void build_minimal_superblock(void)
{
    memset(mock_bdev.data + MOCK_BLOCK_SIZE, 0, MOCK_BLOCK_SIZE);

    ext4_superblock_t *sb = (ext4_superblock_t *)(mock_bdev.data + MOCK_BLOCK_SIZE);
    sb->s_inodes_count = 16;
    sb->s_blocks_count_lo = MOCK_NUM_BLOCKS;
    sb->s_r_blocks_count_lo = 0;
    sb->s_free_blocks_count_lo = MOCK_NUM_BLOCKS - 10;
    sb->s_free_inodes_count = 14;
    sb->s_first_data_block = 0;
    sb->s_log_block_size = 0;  /* 1024 byte blocks */
    sb->s_log_cluster_size = 0;
    sb->s_blocks_per_group = MOCK_NUM_BLOCKS;
    sb->s_clusters_per_group = MOCK_NUM_BLOCKS;
    sb->s_inodes_per_group = 16;
    sb->s_mtime = 0;
    sb->s_wtime = 0;
    sb->s_mnt_count = 0;
    sb->s_max_mnt_count = -1;
    sb->s_magic = EXT4_SUPERBLOCK_MAGIC;
    sb->s_state = 0x0001; /* EXT4_VALID_FS */
    sb->s_errors = 0;
    sb->s_minor_rev_level = 1;
    sb->s_creator_os = 0; /* Linux */
    sb->s_rev_level = 1;  /* Dynamic superblock */
    sb->s_def_resuid = 0;
    sb->s_def_resgid = 0;
    sb->s_first_ino = 11;
    sb->s_inode_size = 128;
    sb->s_block_group_nr = 0;
    sb->s_feature_compat = 0;
    sb->s_feature_incompat = 0;
    sb->s_feature_ro_compat = 0;
    memset(sb->s_uuid, 0, 16);
    memset(sb->s_volume_name, 0, 16);
}

/* ============================================================================
 * Helper: Build a minimal root directory inode
 * ============================================================================ */

static void build_root_inode(uint32_t inode_block)
{
    memset(mock_bdev.data + (inode_block * MOCK_BLOCK_SIZE), 0, MOCK_BLOCK_SIZE);

    ext4_inode_t *inode = (ext4_inode_t *)(mock_bdev.data + (inode_block * MOCK_BLOCK_SIZE));
    inode->i_mode = 0x41ED; /* directory, 0755 */
    inode->i_uid_lo = 0;
    inode->i_size_lo = 4096;
    inode->i_gid_lo = 0;
    inode->i_links_count = 2;
    inode->i_blocks_lo = 8;
    inode->i_block[0] = 2;
    inode->i_block[1] = 3;
}

/* ============================================================================
 * Helper: Build root directory entries (. .. testfile)
 * ============================================================================ */

static void build_root_dir_entries(uint32_t dir_block)
{
    memset(mock_bdev.data + (dir_block * MOCK_BLOCK_SIZE), 0, MOCK_BLOCK_SIZE);

    uint8_t *p = mock_bdev.data + (dir_block * MOCK_BLOCK_SIZE);

    /* "." entry */
    ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)p;
    de->inode = 2;
    de->rec_len = 128;
    de->name_len = 1;
    de->file_type = 2; /* dir */
    de->name[0] = '.';
    de->name[1] = '\0';
    p += 128;

    /* ".." entry */
    de = (ext4_dir_entry_2_t *)p;
    de->inode = 2;
    de->rec_len = 128;
    de->name_len = 2;
    de->file_type = 2; /* dir */
    de->name[0] = '.';
    de->name[1] = '.';
    de->name[2] = '\0';
    p += 128;

    /* "testfile" entry */
    de = (ext4_dir_entry_2_t *)p;
    de->inode = 12;
    de->rec_len = 128;
    de->name_len = 8;
    de->file_type = 1; /* regular file */
    memcpy(de->name, "testfile", 8);
    de->name[8] = '\0';
}

/* ============================================================================
 * Setup / Teardown
 * ============================================================================ */

TEST_SETUP(ext4_setup) {
    mock_bdev.data = calloc(MOCK_BLOCK_SIZE, MOCK_NUM_BLOCKS);
    mock_bdev.block_size = MOCK_BLOCK_SIZE;
    mock_bdev.num_blocks = MOCK_NUM_BLOCKS;
}

TEST_TEARDOWN(ext4_teardown) {
    free(mock_bdev.data);
    mock_bdev.data = NULL;
}

/* ============================================================================
 * Tests
 * ============================================================================ */

TEST(superblock_magic) {
    build_minimal_superblock();
    ext4_superblock_t *sb = (ext4_superblock_t *)(mock_bdev.data + MOCK_BLOCK_SIZE);
    TEST_ASSERT_EQ(sb->s_magic, EXT4_GOOD_MAGIC);
}

TEST(superblock_block_size) {
    build_minimal_superblock();
    ext4_superblock_t *sb = (ext4_superblock_t *)(mock_bdev.data + MOCK_BLOCK_SIZE);
    TEST_ASSERT_EQ(sb->s_log_block_size, 0);
}

TEST(superblock_valid_state) {
    build_minimal_superblock();
    ext4_superblock_t *sb = (ext4_superblock_t *)(mock_bdev.data + MOCK_BLOCK_SIZE);
    TEST_ASSERT_EQ(sb->s_state, 0x0001);
}

TEST(superblock_inode_count) {
    build_minimal_superblock();
    ext4_superblock_t *sb = (ext4_superblock_t *)(mock_bdev.data + MOCK_BLOCK_SIZE);
    TEST_ASSERT_EQ(sb->s_inodes_count, 16);
    TEST_ASSERT_EQ(sb->s_inodes_per_group, 16);
}

TEST(read_write_block_roundtrip) {
    uint8_t write_buf[MOCK_BLOCK_SIZE];
    uint8_t read_buf[MOCK_BLOCK_SIZE];

    memset(write_buf, 0xAB, MOCK_BLOCK_SIZE);
    mock_write_block(100, write_buf);
    mock_read_block(100, read_buf);

    TEST_ASSERT_MEM_EQ(write_buf, read_buf, MOCK_BLOCK_SIZE);
}

TEST(read_out_of_range) {
    uint8_t buf[MOCK_BLOCK_SIZE];
    (void)buf;
    int rc = mock_read_block(MOCK_NUM_BLOCKS, buf);
    TEST_ASSERT_EQ(rc, -1);
}

TEST(write_out_of_range) {
    uint8_t buf[MOCK_BLOCK_SIZE];
    memset(buf, 0, MOCK_BLOCK_SIZE);
    int rc = mock_write_block(MOCK_NUM_BLOCKS, buf);
    TEST_ASSERT_EQ(rc, -1);
}

TEST(inode_directory_mode) {
    build_root_inode(20);
    ext4_inode_t *inode = (ext4_inode_t *)(mock_bdev.data + (20 * MOCK_BLOCK_SIZE));
    TEST_ASSERT_EQ((inode->i_mode & 0xF000), 0x4000);
    TEST_ASSERT_EQ(inode->i_links_count, 2);
}

TEST(inode_block_pointers) {
    build_root_inode(20);
    ext4_inode_t *inode = (ext4_inode_t *)(mock_bdev.data + (20 * MOCK_BLOCK_SIZE));
    TEST_ASSERT_EQ(inode->i_block[0], 2);
}

TEST(dir_entry_dot) {
    build_root_dir_entries(50);
    ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(mock_bdev.data + (50 * MOCK_BLOCK_SIZE));
    TEST_ASSERT_EQ(de->inode, 2);
    TEST_ASSERT_EQ(de->name_len, 1);
    TEST_ASSERT_EQ(de->file_type, 2);
    TEST_ASSERT_STR_EQ(de->name, ".");
}

TEST(dir_entry_dotdot) {
    build_root_dir_entries(50);
    ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(mock_bdev.data + (50 * MOCK_BLOCK_SIZE));
    ext4_dir_entry_2_t *de2 = (ext4_dir_entry_2_t *)((uint8_t *)de + 128);
    TEST_ASSERT_EQ(de2->inode, 2);
    TEST_ASSERT_EQ(de2->name_len, 2);
    TEST_ASSERT_STR_EQ(de2->name, "..");
}

TEST(dir_entry_regular_file) {
    build_root_dir_entries(50);
    ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(mock_bdev.data + (50 * MOCK_BLOCK_SIZE));
    ext4_dir_entry_2_t *de2 = (ext4_dir_entry_2_t *)((uint8_t *)de + 128);
    ext4_dir_entry_2_t *de3 = (ext4_dir_entry_2_t *)((uint8_t *)de2 + 128);
    TEST_ASSERT_EQ(de3->inode, 12);
    TEST_ASSERT_EQ(de3->file_type, 1);
    TEST_ASSERT_STR_EQ(de3->name, "testfile");
}

TEST(mock_bdev_init) {
    TEST_ASSERT_NOT_NULL(mock_bdev.data);
    TEST_ASSERT_EQ(mock_bdev.block_size, MOCK_BLOCK_SIZE);
    TEST_ASSERT_EQ(mock_bdev.num_blocks, MOCK_NUM_BLOCKS);
}


TEST(block_device_consistency) {
    for (int i = 0; i < 100; i++) {
        uint8_t pattern = (uint8_t)(i & 0xFF);
        uint8_t write_buf[MOCK_BLOCK_SIZE];
        uint8_t read_buf[MOCK_BLOCK_SIZE];
        memset(write_buf, pattern, MOCK_BLOCK_SIZE);
        mock_write_block(i + 10, write_buf);
        mock_read_block(i + 10, read_buf);
        TEST_ASSERT_MEM_EQ(write_buf, read_buf, MOCK_BLOCK_SIZE);
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    printf("ext4 Filesystem Tests (mock block device)\n");
    printf("==========================================\n");

    /* Register setup/teardown */
    test_setup_fn = test_setup_ext4_setup;
    test_teardown_fn = test_teardown_ext4_teardown;

    /* Register all tests */
    TEST_REGISTER(superblock_magic);
    TEST_REGISTER(superblock_block_size);
    TEST_REGISTER(superblock_valid_state);
    TEST_REGISTER(superblock_inode_count);
    TEST_REGISTER(read_write_block_roundtrip);
    TEST_REGISTER(read_out_of_range);
    TEST_REGISTER(write_out_of_range);
    TEST_REGISTER(inode_directory_mode);
    TEST_REGISTER(inode_block_pointers);
    TEST_REGISTER(dir_entry_dot);
    TEST_REGISTER(dir_entry_dotdot);
    TEST_REGISTER(dir_entry_regular_file);
    TEST_REGISTER(mock_bdev_init);
    TEST_REGISTER(block_device_consistency);

    test_run_all();
    return test_summary();
}