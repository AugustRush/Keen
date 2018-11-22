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
#define INVALID_LOC 0xFFFFFFFF

typedef struct KeenInfo {
    uint64_t hash;
    uint32_t count;
    uint32_t piece_start;
    uint32_t piece_used_count;
    uint32_t piece_end;
    uint32_t head_node_index;
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
    uint32_t super_piece_loc;
    KeenNode nodes[PIECE_NODE_MAX];
} KeenPiece;

typedef struct Keen {
    BYTE *raws;
    KeenInfo *info;
    KeenPiece *head_piece;
    BYTE *contents;
} Keen;

const size_t NODE_SIZE = sizeof(KeenNode);
const size_t PIECE_SIZE = sizeof(KeenNode);


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
        info->piece_start = sizeof(KeenInfo) * 2;
        info->piece_used_count = 0;
        info->piece_end = PAGE_SIZE;
        info->head_node_index = 0;
        info->contents_start = PAGE_SIZE;
        info->contents_used = 0;
        info->contents_end = capacity;
        keen->head_piece = (KeenPiece *)(raws + info->piece_start);
        keen->head_piece->count = 0;
        keen->head_piece->super_piece_loc = INVALID_LOC;
        keen->contents = raws + info->contents_start;
    }
    
    return keen;
}

KeenPiece *_getPieceAtIndex(KeenRef keen, uint32_t index) {
    KeenPiece *pieceList = (KeenPiece *)(keen->raws + keen->info->piece_start);
    return &(pieceList[index]);
}


KeenNode * _pieceReplaceNodeAtIndex(KeenPiece *piece, int index, uint64_t hash, int32_t key_length, int32_t value_length, int32_t content_raw_start) {
    KeenNode *node = &(piece->nodes[index]);
    node->invalid = true;
    node->hash = hash;
    node->key_length = key_length;
    node->value_length = value_length;
    node->raw_start = content_raw_start;
    node->leaf_lhs_loc = INVALID_LOC;
    node->leaf_rhs_loc = INVALID_LOC;
    return node;
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
        memcpy(address + 1, address, NODE_SIZE * offset);
    }
    
    _pieceReplaceNodeAtIndex(piece, insert_index, hash, key_length, value_length, content_raw_start);
}

KeenNode * _pieceInsertNodeAtIndex(KeenPiece *piece, int index, uint64_t hash, const char *key, const void *value, int32_t key_length, int32_t value_length) {
    // move
    int offset = piece->count - index;
    KeenNode *src = &(piece->nodes[index]);
    if (offset > 0) {
        memmove(src + 1, src, NODE_SIZE * offset);
    }
    piece->count += 1;
    
    return src;
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

void _updatePieceCount(KeenRef keen, int32_t num) {
    KeenInfo *info = keen->info;
    info->piece_used_count += num;
    if ((info->piece_start + PIECE_SIZE * info->piece_used_count - info->piece_end) < PIECE_SIZE) {
        // need exten file
        printf("need extend file");
    }
}

void _splitPiecePromoteNodeFromLeaf(KeenRef keen, KeenNode *promote_node, KeenPiece *from_piece, uint32_t piece_loc) {
    KeenNode *first_node = &(from_piece->nodes[0]);
    KeenNode *last_node = &(from_piece->nodes[from_piece->count - 1]);
    if (first_node->hash > promote_node->hash) {
        if (from_piece->count < PIECE_NODE_MAX) {
            KeenNode *dst_node = &(from_piece->nodes[1]);
            memmove(dst_node, from_piece->nodes, from_piece->count * NODE_SIZE);
            memcpy(from_piece->nodes, promote_node, NODE_SIZE);
            dst_node->leaf_lhs_loc = promote_node->leaf_rhs_loc;
        } else {
            int mid = (PIECE_NODE_MAX + 1) / 2;
            int copy_count = (PIECE_NODE_MAX - mid - 1);
            KeenNode *new_prom_node = &(from_piece->nodes[mid]);
            // make new rhs piece
            KeenPiece *rhs_new_piece = _getPieceAtIndex(keen, keen->info->piece_used_count);
            memcpy(rhs_new_piece->nodes, &(new_prom_node[1]), NODE_SIZE * copy_count);
            rhs_new_piece->count = copy_count;
            //
            new_prom_node->leaf_lhs_loc = piece_loc;
            new_prom_node->leaf_rhs_loc = keen->info->piece_used_count;
            //
            _updatePieceCount(keen, 1);
            // insert node current piece
            memmove(&(from_piece->nodes[1]), from_piece->nodes, NODE_SIZE * (PIECE_NODE_MAX - copy_count));
            memcpy(from_piece->nodes, promote_node, NODE_SIZE);
            // reset count
            from_piece->count -= copy_count;
            // promote new node to super
            if (from_piece->super_piece_loc < INVALID_LOC) { // has super piece
                KeenPiece *super_piece = _getPieceAtIndex(keen, from_piece->super_piece_loc);
                _splitPiecePromoteNodeFromLeaf(keen, promote_node, super_piece,from_piece->super_piece_loc);
            } else {
                // make new rhs piece
                KeenPiece *new_head_piece = _getPieceAtIndex(keen, keen->info->piece_used_count);
                memcpy(new_head_piece->nodes, new_prom_node, NODE_SIZE);
                new_head_piece->count = 1;
                keen->info->head_node_index = keen->info->piece_used_count;
                _updatePieceCount(keen, 1);
            }
        }
    } else if (last_node->hash < promote_node->hash) {
        if (from_piece->count < PIECE_NODE_MAX) {
            
        } else {
            
        }
    }
}

void _findFitPieceInsertOrUpdate(KeenRef keen, KeenPiece *from_piece, uint64_t hash, const char *key, const void *value, int32_t key_length, int32_t value_length) {
    KeenNode *first_node = &(from_piece->nodes[0]);
    KeenNode *last_node = &(from_piece->nodes[from_piece->count - 1]);
    
    if (first_node->hash > hash) {
        if (first_node->leaf_lhs_loc < INVALID_LOC) { // has leafs
            KeenPiece *next_piece = _getPieceAtIndex(keen, first_node->leaf_lhs_loc);
            _findFitPieceInsertOrUpdate(keen, next_piece, hash, key, value, key_length, value_length);
        } else {
            // no leafs
            if (from_piece->count < PIECE_NODE_MAX) {
                if (from_piece->count == 0) {
                    from_piece->super_piece_loc = INVALID_LOC;
                }
                // insert current piece
                KeenNode *node = _pieceInsertNodeAtIndex(from_piece, 0, hash, key, value, key_length, value_length);
                node->invalid = true;
                node->hash = hash;
                node->key_length = key_length;
                node->value_length = value_length;
                node->leaf_lhs_loc = INVALID_LOC;
                node->leaf_rhs_loc = node[1].leaf_lhs_loc;
            } else {
                int mid = (PIECE_NODE_MAX + 1) / 2;
                int copy_count = (PIECE_NODE_MAX - mid - 1);
                KeenNode *promote_node = &(from_piece->nodes[mid]);
                // make new rhs piece
                KeenPiece *rhs_new_piece = _getPieceAtIndex(keen, keen->info->piece_used_count);
                memcpy(rhs_new_piece->nodes, &(promote_node[1]), NODE_SIZE * copy_count);
                rhs_new_piece->count = copy_count;
                //
                promote_node->leaf_lhs_loc = keen->info->head_node_index;
                promote_node->leaf_rhs_loc = keen->info->piece_used_count;
                //
                _updatePieceCount(keen, 1);
                // insert node current piece
                memmove(&(from_piece->nodes[1]), from_piece->nodes, NODE_SIZE * (PIECE_NODE_MAX - copy_count));
                KeenNode *insert_node = &(from_piece->nodes[0]);
                insert_node->hash = hash;
                insert_node->key_length = key_length;
                insert_node->value_length = value_length;
                insert_node->raw_start = keen->info->contents_used;
                // reset count
                from_piece->count -= copy_count;
                //
                if (from_piece->super_piece_loc < INVALID_LOC) { // has super piece
                    KeenPiece *super_piece = _getPieceAtIndex(keen, from_piece->super_piece_loc);
                    _splitPiecePromoteNodeFromLeaf(keen, promote_node, super_piece,from_piece->super_piece_loc);
                } else {
                    // make new rhs piece
                    KeenPiece *new_head_piece = _getPieceAtIndex(keen, keen->info->piece_used_count);
                    memcpy(new_head_piece->nodes, promote_node, NODE_SIZE);
                    new_head_piece->count = 1;
                    keen->info->head_node_index = keen->info->piece_used_count;
                    _updatePieceCount(keen, 1);
                }
                
            }
                
        }
    } else if (last_node->hash < hash) {
        if (last_node->leaf_rhs_loc < INVALID_LOC) {
            KeenPiece*next_piece = _getPieceAtIndex(keen, last_node->leaf_rhs_loc);
            _findFitPieceInsertOrUpdate(keen, next_piece, hash, key, value, key_length, value_length);
        } else {
            //
            if (from_piece->count < PIECE_NODE_MAX) {

            } else {
                
            }
        }
    } else {
        
        for (uint8_t i = 0; i < from_piece->count - 1; i++) {
            KeenNode *current = &(from_piece->nodes[i]);
            KeenNode *next = &(from_piece->nodes[i + 1]);
            if (current->hash == hash) {
                //update or resolve conflict
                printf("update or resolve conflict.....");
                break;
            }
            
            if (next->hash == hash) {
                //update or resolve conflict
                printf("update or resolve conflict.....");
                break;
            }
            
            if (current->hash < hash && next->hash > hash) {
                if (current->leaf_rhs_loc < INVALID_LOC) { // has leafs
                    KeenPiece *next_piece = _getPieceAtIndex(keen, current->leaf_rhs_loc);
                    _findFitPieceInsertOrUpdate(keen, next_piece, hash, key, value, key_length, value_length);
                } else {
                    
                }
            }
        }
    }
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
        if (piece->count == 0) { // add first node
            _pieceReplaceNodeAtIndex(piece, 0, hash, (uint32_t)key_length, value_length, 0);
            _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
            piece->count = 1;
            keen->info->piece_used_count += 1;
        } else {
            _findFitPieceInsertOrUpdate(keen, piece, hash, key, value, (uint32_t)key_length, value_length);
            _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
        }
//        if (piece->count < PIECE_NODE_MAX) {
//            if (piece->count > 0) {
//                // insert new node
//                _pieceInsertNewNode(piece, hash, (uint32_t)key_length, value_length, keen->info->contents_used);
//                piece->count = piece->count + 1;
//                _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
//            } else {
//                //add first element
//                _pieceReplaceNodeAtIndex(piece, 0, hash, (uint32_t)key_length, value_length, keen->info->contents_used);
//                piece->count = piece->count + 1;
//                _keenFillContentRaws(keen, key, value, (uint32_t)key_length, value_length, value_type);
//                keen->info->piece_used_count = keen->info->piece_used_count + 1;
//            }
//        } else {
//            //
//            KeenInfo *info = keen->info;
//            int mid_index = piece->count / 2;
//            int move_count = piece->count - mid_index - 1;
//            KeenNode *mid_node = &(piece->nodes[mid_index]);
//            if (mid_node->hash > hash) {
//                KeenPiece *pieceList = (KeenPiece *)(keen->raws + info->piece_start);
//                KeenPiece *left_piece = piece;
//                piece = &(pieceList[info->piece_used_count]);
//                KeenPiece *right_piece = &(pieceList[info->piece_used_count + 1]);
//                //fill node
//                KeenNode *top = _pieceReplaceNodeAtIndex(piece, 0, hash, (uint32_t)key_length, value_length, keen->info->contents_used);
//                top->leaf_lhs_loc = info->head_node_index;
//                top->leaf_rhs_loc = info->piece_used_count + 1;
//                info->head_node_index = info->piece_used_count;
//                //move src nodes to dst nodes
//                memcpy(mid_node, right_piece->nodes, NODE_SIZE * move_count);
//                left_piece->count = left_piece->count - move_count;
//                right_piece->count = move_count;
//                //update piece used count
//                info->piece_used_count += 2;
//
//
//            } else {
//
//            }
//        }
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
