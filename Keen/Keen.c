//
//  Keen.c
//  Keen
//
//  Created by pingwei liu on 2018/11/21.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#include "Keen.h"
#include "xxhash/xxhash.h"
#include <string.h>

#define KEEN_DEBUG 1
#define PAGE_SIZE 4096
#define PIECE_NODE_MAX 4

typedef struct KeenInfo {
    uint64_t hash;
    uint32_t count;
    uint32_t node_start;
    uint32_t node_used;
    uint32_t node_end;
    uint32_t contents_start;
    uint32_t contents_used;
    uint32_t contents_end;
} KeenInfo;


typedef struct __attribute__((__packed__)) KeenNode {
    bool invalid;
    uint64_t hash;
    uint32_t key_length;
    uint32_t value_length;
    uint32_t raw_start;
    uint32_t leaf_lhs_loc;
    uint32_t leaf_rhs_loc;
} KeenNode;

typedef struct __attribute__((__packed__)) KeenPiece {
    uint8_t count;
    KeenNode nodes[PIECE_NODE_MAX];
} KeenPiece;

typedef struct Keen {
    BYTE *raws;
    KeenInfo *info;
    KeenPiece *head_piece;
    BYTE *contents;
} Keen;


KeenRef KeenRefNew(void) {
    int32_t capacity = PAGE_SIZE * 2 * sizeof(int8_t);
    BYTE* raws = calloc(1,capacity);
    KeenRef keen = malloc(sizeof(Keen));
    keen->raws = raws;
    keen->info = (KeenInfo *)raws;
    //
    KeenInfo *info = keen->info;
    uint64_t hashCode = XXH64("keen_invalid_header", 20, 0);
    if (info->hash != hashCode) {
        info->hash = hashCode;
        info->count = 1;
        info->node_start = sizeof(KeenInfo) * 2;
        info->node_used = 0;
        info->node_end = PAGE_SIZE;
        info->contents_start = PAGE_SIZE;
        info->contents_used = 0;
        info->contents_end = capacity;
        keen->head_piece = (KeenPiece *)(raws + info->node_start);
        keen->head_piece->count = 0;
        keen->contents = raws + info->contents_start;
    }
    
    return keen;
}

//void _pieceAddNewNode(KeenPiece *piece, uint64_t hash, int32_t key_length, int32_t value_length, int32_t content_raw_start) {
//    KeenNode *node = &(piece->nodes[piece->count]);
//    node->invalid = true;
//    node->hash = hash;
//    node->key_length = key_length;
//    node->value_length = value_length;
//    node->raw_start = content_raw_start;
//    //
//    piece->count = piece->count + 1;
//}


void _pieceReplaceNodeAtIndex(KeenPiece *piece, int index, uint64_t hash, int32_t key_length, int32_t value_length, int32_t content_raw_start) {
    KeenNode *node = &(piece->nodes[index]);
    node->invalid = true;
    node->hash = hash;
    node->key_length = key_length;
    node->value_length = value_length;
    node->raw_start = content_raw_start;
}

void _pieceInsertNewNode(KeenPiece *piece, uint64_t hash, int32_t key_length, int32_t value_length, int32_t content_raw_start) {
    int insert_index = piece->count;
    KeenNode *address = NULL;
    for (int i = 0; i < piece->count; i++) {
        KeenNode *node = &(piece->nodes[i]);
        if (node->hash > hash) {
            insert_index = i;
            address = node;
            break;
        }
    }
    // move
    int offset = piece->count - insert_index;
    if (offset > 0) {
        memcpy(address + 1, address, sizeof(KeenNode) * offset);
    }
    
    _pieceReplaceNodeAtIndex(piece, insert_index, hash, key_length, value_length, content_raw_start);
}


void _keenFillContentRaws(KeenRef keen, const char *key ,const void *raws, int32_t key_length, int32_t value_length, uint8_t type) {
    KeenInfo *info = keen->info;
    int32_t after_used = info->contents_used + key_length + 1 + value_length;
    if (after_used > info->contents_end - info->contents_start) {
        // need extend file capacity
    }
    BYTE *buffer = (BYTE *)raws;
    memcpy(keen->contents + keen->info->contents_used, key, key_length);// key
    keen->contents[keen->info->contents_used + key_length] = type; // type
    memcpy(keen->contents + keen->info->contents_used + key_length + 1, buffer, value_length); // value
    //update after used
    info->contents_used = after_used;
}

const BYTE *_keenGetContent(KeenRef keen, const char *key, uint32_t key_length, int32_t raw_start) {
    char *key_buf = (char *)(keen->contents + raw_start);
    if (strcmp(key, key_buf) == 0) {
        return keen->contents + raw_start + key_length;
    }
    
    return NULL;
}

void KeenRefAddItem(KeenRef keen, const char *key, const void *value, int32_t value_length, uint8_t value_type) {
    if (keen != NULL && keen->raws != NULL) {
        unsigned long key_length = strlen(key) + 1;
        uint64_t hash = XXH64(key, key_length, 0);
        KeenPiece *piece = keen->head_piece;
        
        if (piece->count < PIECE_NODE_MAX) {
            if (piece->count > 0) {
                // insert nre node
                _pieceInsertNewNode(piece, hash, (uint32_t)key_length, value_length, keen->info->contents_used);
                piece->count = piece->count + 1;
                _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
            } else {
                //add first element
                _pieceReplaceNodeAtIndex(piece, 0, hash, (uint32_t)key_length, value_length, keen->info->contents_used);
                piece->count = piece->count + 1;
                _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
            }
        } else {
            
        }
    }
}


const BYTE* KeenRefGetItem(KeenRef keen, const char *key) {
    if (keen != NULL && keen->raws != NULL) {
        unsigned long key_length = strlen(key) + 1;
        uint64_t hash = XXH64(key, key_length, 0);
        KeenPiece *piece = keen->head_piece;
        if (piece->count > 0) {
            for (int i = 0; i < piece->count; i++) {
                KeenNode node = piece->nodes[i];
                if (node.hash == hash) {
                    return _keenGetContent(keen, key, (uint32_t)key_length, node.raw_start);
                }
            }
        }
        
//        if (head->hash == hash) {
//            char *key_buf = alloca(key_length);
//            memcpy(key_buf, keen->contents + head->raw_start, key_length);
//            if (strcmp(key_buf, key) == 0) {
//                return keen->contents + head->raw_start + key_length;
//            }
//        }
//
//        if (head->hash > hash) {
//
//        }
//
//        if (head->hash < hash) {
//
//        }
    }
    
    return NULL;
}
