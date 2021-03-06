/* --------------------------------------------------------------------------
 * Project C - BTree Index
 * EECS 339, Fall 2012, Prof. Dinda
 *
 *
 * Lizz Bartos - eab879
 * Stephen Duranski -
 * Alex Jablonski - amj650
 * 
 ---------------------------------------------------------------------------- */

#include <assert.h>
#include <string.h>
#include "btree.h"
KeyValuePair::KeyValuePair()
{}


KeyValuePair::KeyValuePair(const KEY_T &k, const VALUE_T &v) : 
  key(k), value(v)
{}


KeyValuePair::KeyValuePair(const KeyValuePair &rhs) :
  key(rhs.key), value(rhs.value)
{}


KeyValuePair::~KeyValuePair()
{}


KeyValuePair & KeyValuePair::operator=(const KeyValuePair &rhs)
{
  return *( new (this) KeyValuePair(rhs));
}

// Constructor
BTreeIndex::BTreeIndex(SIZE_T keysize,
                       SIZE_T valuesize,
                       BufferCache *cache,
                       bool unique)
{
    superblock.info.keysize=keysize;
    superblock.info.valuesize=valuesize;
    buffercache=cache;
    // note: ignoring unique now
}

// Default constructor
BTreeIndex::BTreeIndex()
{
  // shouldn't have to do anything
}


//
// Note, will not attach!
//
BTreeIndex::BTreeIndex(const BTreeIndex &rhs)
{
    buffercache=rhs.buffercache;
    superblock_index=rhs.superblock_index;
    superblock=rhs.superblock;
}

// Destructor
BTreeIndex::~BTreeIndex()
{
  // shouldn't have to do anything
}

// Override = operator
BTreeIndex & BTreeIndex::operator=(const BTreeIndex &rhs)
{
  return *(new(this)BTreeIndex(rhs));
}

/*
 * AllocateNode(SIZE_T &n)
 * Takes the first free node in the freelist
 * assume freelist has all the nodes that are available
 * if nothing available will return error
 */
ERROR_T BTreeIndex::AllocateNode(SIZE_T &n)
{
    n=superblock.info.freelist;
    
    // make sure node is not zero because that would mean
    // we havent allocated a root node yet
    if (n==0)
    {
        return ERROR_NOSPACE;
    }
    
    BTreeNode node;
    
    node.Unserialize(buffercache,n);
    
    assert(node.info.nodetype==BTREE_UNALLOCATED_BLOCK);
    
    superblock.info.freelist=node.info.freelist;
    
    superblock.Serialize(buffercache,superblock_index);
    
    buffercache->NotifyAllocateBlock(n);
    
    return ERROR_NOERROR;
}

/*
 * DeallocateNode(const SIZE_T &n)
 *
 */
ERROR_T BTreeIndex::DeallocateNode(const SIZE_T &n)
{
    BTreeNode node;
    
    node.Unserialize(buffercache,n);
    
    assert(node.info.nodetype!=BTREE_UNALLOCATED_BLOCK);
    
    node.info.nodetype=BTREE_UNALLOCATED_BLOCK;
    
    node.info.freelist=superblock.info.freelist;
    
    node.Serialize(buffercache,n);
    
    superblock.info.freelist=n;
    
    superblock.Serialize(buffercache,superblock_index);
    
    buffercache->NotifyDeallocateBlock(n);
    
    return ERROR_NOERROR;
}

// This is called before any inserts, updates, or deletes happen
// If create=true, then initblock is meaningless
// If create=false, than the index already exists and we are telling you
// the block that the last detach returned
// This should be your superblock, which contains the information
// you need to find the elements of the tree.
// return zero on success or ERROR_NOTANINDEX if we are
// giving you an incorrect block to start with

/*
 * Name:    Attach
 * Purpose: open a BTree for use.
 * Params:  const SIZE_T initblock
            const bool create
 * Returns: 
 */
// In our API, attach is combined with initialization (the "create" argument)
// You can ask for the BTree to be attached with or without initialization.
// This is like mount in a file system.
ERROR_T BTreeIndex::Attach(const SIZE_T initblock, const bool create)
{
  ERROR_T rc;

  superblock_index=initblock;
  assert(superblock_index==0);
    
    /*
     You will also want to have a data structure
     to keep track of free and allocated blocks on the disk.
     Since you are not going to be graded on performance,
     I suggest you use a simple free list.
     Have the superblock point to the first free node
     and have every free node point to the next free node.
     Then simply insert and remove free nodes from the front of the list.
     */

  if (create) {
    // build a super block, root node, and a free space list
    //
    // Superblock at superblock_index
    // root node at superblock_index+1
    // free space list for rest
    BTreeNode newsuperblock(BTREE_SUPERBLOCK,
			    superblock.info.keysize,
			    superblock.info.valuesize,
			    buffercache->GetBlockSize());
    newsuperblock.info.rootnode=superblock_index+1;
    newsuperblock.info.freelist=superblock_index+2;
    newsuperblock.info.numkeys=0;

    buffercache->NotifyAllocateBlock(superblock_index);

    rc=newsuperblock.Serialize(buffercache,superblock_index);

    if (rc) {  return rc;  }
    
    BTreeNode newrootnode(BTREE_ROOT_NODE,
			  superblock.info.keysize,
			  superblock.info.valuesize,
			  buffercache->GetBlockSize());
    newrootnode.info.rootnode=superblock_index+1;
    newrootnode.info.freelist=superblock_index+2;
    newrootnode.info.numkeys=0;

    buffercache->NotifyAllocateBlock(superblock_index+1);

    rc=newrootnode.Serialize(buffercache,superblock_index+1);

    if (rc) {  return rc;  }

    for (SIZE_T i=superblock_index+2; i<buffercache->GetNumBlocks();i++)
    {
      BTreeNode newfreenode(BTREE_UNALLOCATED_BLOCK,
			    superblock.info.keysize,
			    superblock.info.valuesize,
			    buffercache->GetBlockSize());
      newfreenode.info.rootnode=superblock_index+1;
      newfreenode.info.freelist= ((i+1)==buffercache->GetNumBlocks()) ? 0: i+1;
      
      rc = newfreenode.Serialize(buffercache,i);

      if (rc) {  return rc;  }

    }
  }

  // OK, now, mounting the btree is simply a matter of reading the superblock 

  return superblock.Unserialize(buffercache,initblock);
}
    

// This is called after all inserts, updates, or deletes are done.
// We expect you to tell us the number of your superblock, which
// we will return to you on the next attach
/*
 * Detach(SIZE_T &initblock)
 *
 */
ERROR_T BTreeIndex::Detach(SIZE_T &initblock)
{
  return superblock.Serialize(buffercache,superblock_index);
}


/*
 * Name:    Display
 * Purpose: do a traversal of the BTree, printing out the sorted (key,value)
 *          pairs in ascending order of the keys
 * Params:  (ostream &o, BTreeDisplayType display_type)
 */
ERROR_T BTreeIndex::Display(ostream &o, BTreeDisplayType display_type) const
{
    ERROR_T rc;
    if (display_type==BTREE_DEPTH_DOT)
    {
        o << "digraph tree { \n";
    }
    rc=DisplayInternal(superblock.info.rootnode,o,display_type);
    if (display_type==BTREE_DEPTH_DOT)
    {
        o << "}\n";
    }
    return ERROR_NOERROR;
}


/*
 * Name:    Lookup
 * Purpose: return the value associated with the key
 * Params:  const KEY_T &key,
 *          VALUE_T &value
 */
ERROR_T BTreeIndex::Lookup(const KEY_T &key, VALUE_T &value)
{
    return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_LOOKUP, key, value);
}


/*
 * Name:    Update
 * Purpose: change the value associated with an existing key
 * Params:  const KEY_T &key
 *          const VALUE_T &value
 */
ERROR_T BTreeIndex::Update(const KEY_T &key, const VALUE_T &value)
{
    // WROTE ME
    VALUE_T valueWritable = value;
    return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_UPDATE, key, valueWritable);
}


/*
 * LookupOrUpdateInternal(const SIZE_T &node, const BTreeOp op,
                          const KEY_T &key,VALUE_T &value)
 *
 */
ERROR_T BTreeIndex::LookupOrUpdateInternal(const SIZE_T &node,
					   const BTreeOp op,
					   const KEY_T &key,
					   VALUE_T &value)
{
    BTreeNode b;
    ERROR_T rc;
    SIZE_T offset;
    KEY_T testkey;
    SIZE_T ptr;
    
    rc= b.Unserialize(buffercache,node);
    
    if (rc!=ERROR_NOERROR)
    {
        return rc;
    }
    
    switch (b.info.nodetype)
    {
        //
        // Internal nodes:
        // store keys and pointers (disk block #) to other disk blocks
        //
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            // Scan through key/ptr pairs
            // and recurse if possible
            for (offset=0;offset<b.info.numkeys;offset++)
            {
                rc=b.GetKey(offset,testkey);
                if (rc) {  return rc; }
                if (key<testkey || key==testkey)
                {
                    // OK, so we now have the first key that's larger
                    // so we ned to recurse on the ptr immediately previous to
                    // this one, if it exists
                    rc=b.GetPtr(offset,ptr);
                    if (rc) { return rc; }
                    return LookupOrUpdateInternal(ptr,op,key,value);
                }
            }
            // if we got here, we need to go to the next pointer, if it exists
            if (b.info.numkeys>0)
            {
                rc=b.GetPtr(b.info.numkeys,ptr);
                if (rc) { return rc; }
                return LookupOrUpdateInternal(ptr,op,key,value);
            }
            else
            {
                // There are no keys at all on this node, so nowhere to go
                return ERROR_NONEXISTENT;
            }
            break;
            
        //
        // Leaf nodes: store keys and their associated values
        //
        case BTREE_LEAF_NODE:
            // Scan through keys looking for matching value
            for (offset=0;offset<b.info.numkeys;offset++)
            {
                rc=b.GetKey(offset,testkey);
                if (rc) {  return rc; }
                if (testkey==key) {
                    if (op==BTREE_OP_LOOKUP)
                    {
                        return b.GetVal(offset,value);
                    }
                    else
                    {
                        ERROR_T rc = b.SetVal(offset, value);
                        if (rc) 
                            return rc;
                        return b.Serialize(buffercache, node);
                        // WROTE ME
                    }
                }
            }
            return ERROR_NONEXISTENT;
            break;
        default:
            // We can't be looking at anything other than
            // a root, internal, or leaf [node]
            return ERROR_INSANE;
            break;
    }  
    
    return ERROR_INSANE;
}


/*
 * Name:    Delete
 * Purpose: delete the key/value pairassociated with the given key
 * Params:  const KEY_T &key
 * Returns:
 */
ERROR_T BTreeIndex::Delete(const KEY_T &key)
{
    // This is optional extra credit for F12
    //
    //
    return ERROR_UNIMPL;
}


/*
 * Name:    Insert
 * Purpose: insert the key/value pair
 * Params:  const KEY_T &key,
 *          const VALUE_T &value
 * Returns: 
 */
ERROR_T BTreeIndex::Insert(const KEY_T &key, const VALUE_T &value)
{
    // Insertion of existing keys should fail (update is the appropriate operation)

    ERROR_T error;
    BTreeNode root;
    root.Unserialize(buffercache,superblock.info.rootnode);

    if (root.info.numkeys == 0) { // This is the case when root is empty
        BTreeNode leaf(BTREE_LEAF_NODE, 
            superblock.info.keysize,
            superblock.info.valuesize,
            buffercache->GetBlockSize());
        
        SIZE_T leftNode;
        SIZE_T rightNode;
        // Allocate the beginning leaf nodes of root
        if ((error = AllocateNode(leftNode)) != ERROR_NOERROR)
            return error;
        if ((error = AllocateNode(rightNode)) != ERROR_NOERROR)
            return error;
        // Write these new blocks to the disk as leafs
        // (see Attach for how the root and superblock are initialized
        // and written - AllocateNode does not handle all of it!)
        leaf.Serialize(buffercache, leftNode); 
        leaf.Serialize(buffercache, rightNode);
        root.info.numkeys += 1;
        root.SetKey(0, key);
        root.SetPtr(0, leftNode);
        root.SetPtr(1, rightNode);
        root.Serialize(buffercache, superblock.info.rootnode);
    } 

    VALUE_T temp;
    SIZE_T oldRoot=superblock.info.rootnode, newNode;
    KEY_T splitKey;

    BTreeNode interior(BTREE_INTERIOR_NODE, 
        superblock.info.keysize,
        superblock.info.valuesize,
        buffercache->GetBlockSize());

    if (ERROR_NONEXISTENT == Lookup(key, temp)) {
        error = PlaceKeyVal(superblock.info.rootnode, superblock.info.rootnode, key, value);
        if (IsNodeFull(superblock.info.rootnode)) {
            SplitNode(oldRoot, newNode, splitKey);
            // Load the node data into Interior nodes (indead of root nodes)
            // and then save this new node status onto disk (for both nodes)
            interior.Unserialize(buffercache, oldRoot);
            interior.Serialize(buffercache, oldRoot);
            interior.Unserialize(buffercache, newNode);
            interior.Serialize(buffercache, newNode);

            // Make new root node
            if ((error = AllocateNode(superblock.info.rootnode)) != ERROR_NOERROR)
                return error;
            root.info.numkeys = 1;
            root.SetKey(0, splitKey);
            root.SetPtr(0, oldRoot);
            root.SetPtr(1, newNode);
            root.Serialize(buffercache, superblock.info.rootnode);
        }
        return error;
    }
    else
        return ERROR_CONFLICT;
}


// Here you should figure out if your index makes sense
// Is it a tree?  Is it in order?  Is it balanced?  Does each node have
// a valid use ratio?
/*
 * Name:    SanityCheck() const
 * Purpose: do a self-check of the tree looking for problems --- 
 *          this is like "chkdsk" or "fsck" in a file system
 * Params:  none
 *
 * TODO................................................................
 */
ERROR_T BTreeIndex::SanityCheck() const
{
    // WRITE ME
    return ERROR_UNIMPL;
}


/*
 * PlaceKeyVal
 *
 * Tries to place a key-value pair in the BTree
 */
ERROR_T BTreeIndex::PlaceKeyVal(SIZE_T node, SIZE_T parent, const KEY_T &key, const VALUE_T &value)
{
    BTreeNode b;
    ERROR_T rc;
    SIZE_T offset;
    KEY_T testkey;
    SIZE_T ptr;
    // Only used in splitting
    SIZE_T newNode;
    KEY_T splitKey;

    b.Unserialize(buffercache, node); 
    switch (b.info.nodetype) {
        // Internal nodes:
        // store keys and pointers (disk block #) to other disk blocks
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            // Scan through key/ptr pairs
            // and recurse if possible
            for (offset=0;offset<b.info.numkeys;offset++)
            {
                rc=b.GetKey(offset,testkey);
                if (rc) {  return rc; }
                if (key<testkey || key==testkey) {
                    // OK, so we now have the first key that's larger
                    // so we ned to recurse on the ptr immediately previous to
                    // this one, if it exists
                    rc=b.GetPtr(offset,ptr);
                    if (rc) { return rc; }
                    rc=PlaceKeyVal(ptr, node, key, value);
                    if (rc) { return rc; }
                    if (IsNodeFull(ptr)) {
                        rc = SplitNode(ptr, newNode, splitKey);
                        if (rc) { return rc; }
                        return AddNewKeyPtr(node, splitKey, newNode);
                    } else {
                        return rc;
                    }
                }
            }
            // if we got here, we need to go to the next pointer, if it exists
            if (b.info.numkeys>0) {
                rc=b.GetPtr(b.info.numkeys,ptr);
                if (rc) { return rc; }
                rc=PlaceKeyVal(ptr, node, key, value);
                if (rc) { return rc; }
                if (IsNodeFull(ptr)) {
                    rc = SplitNode(ptr, newNode, splitKey);
                    if (rc) { return rc; }
                    return AddNewKeyPtr(node, splitKey, newNode);
                } else {
                    return rc;
                }
            } else {
                // There are no keys at all on this node, so nowhere to go
                return ERROR_NONEXISTENT;
            }
            break;
            
        // Leaf nodes: store keys and their associated values
        //
        // Since part of the invariant we've established is that no node
        // will ever be completely full, there will always be space for a new 
        // key-value pair in a leaf node, so we don't need to do anything extra.
        //
        // The recursive call that led us to this leaf node will handle checking if
        // the leaf node is full after this new key-value pair is inserted, and
        // thus the invariant is maintained
        case BTREE_LEAF_NODE:
            return AddNewKeyVal(node, key, value);
            break;

        default:
            // We can't be looking at anything other than
            // a root, internal, or leaf [node]
            return ERROR_INSANE;
            break;
    }  
    
    return ERROR_INSANE;
}


/*
 * AddNewKeyPtr
 *
 * Adds new key-pointer pair to an interior node
 *
 * (passes a dummy value for the 3rd argument to AddKeyPtrVal)
 */
ERROR_T BTreeIndex::AddNewKeyPtr(const SIZE_T node, const KEY_T &splitKey, SIZE_T newNode)
{
    return AddKeyPtrVal(node, splitKey, VALUE_T(), newNode);
}


/*
 * AddNewKeyVal
 *
 * Adds a new key-value pair to a leaf node
 *
 * (passes a dummy value for the 4th argument to AddKeyPtrVal)
 */
ERROR_T BTreeIndex::AddNewKeyVal(const SIZE_T node, const KEY_T &key, const VALUE_T &value)
{
    return AddKeyPtrVal(node, key, value, 0);
}


/*
 * AddKeyPtrVal
 *
 * Puts data into a node. This handles the logic for LEAF, INTERIOR, and ROOT node insertion
 */
ERROR_T BTreeIndex::AddKeyPtrVal(const SIZE_T node, const KEY_T &key, const VALUE_T &value, SIZE_T newNode)
{
    BTreeNode b;
    b.Unserialize(buffercache, node);
    KEY_T testkey;
    SIZE_T entriesToCopy;
    SIZE_T numkeys = b.info.numkeys;
    SIZE_T offset;
    ERROR_T rc;
    SIZE_T entrySize;

    // Set entry size, and check for valid node type
    switch (b.info.nodetype) {
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            entrySize = b.info.keysize + sizeof(SIZE_T);
            break;
        case BTREE_LEAF_NODE:
            entrySize = b.info.keysize + b.info.valuesize;
            break;
        default: // Invalid node type
            return ERROR_INSANE;
    }

    b.info.numkeys++;
    if (numkeys > 0) {
        for (offset=0, entriesToCopy = numkeys;
                offset < numkeys;
                offset++, entriesToCopy--) {
            if ((rc = b.GetKey(offset, testkey)))
                return rc;
            // If the new key is less than the current key, shift all greater keys up
            // and put this new key in the current location
            if (key < testkey) {
                void *src = b.ResolveKey(offset);
                void *dest = b.ResolveKey(offset + 1);
                memmove(dest, src, entriesToCopy * entrySize);
                if (b.info.nodetype == BTREE_LEAF_NODE) {
                    if ((rc = b.SetKey(offset, key)) || (rc = b.SetVal(offset, value)))
                        return rc;
                } else {
                    if ((rc = b.SetKey(offset, key)) || (rc = b.SetPtr(offset+1, newNode)))
                        return rc;
                }
                break;
            }
            if (offset == numkeys - 1) { 
                // If we are on the last key, and the new key was not less than this one, 
                // then the new key becomes the last key
                if (b.info.nodetype == BTREE_LEAF_NODE) {
                    if ((rc = b.SetKey(numkeys, key)) || (rc = b.SetVal(numkeys, value)))
                        return rc;
                } else {
                    if ((rc = b.SetKey(numkeys, key)) || (rc = b.SetPtr(numkeys+1, newNode)))
                        return rc;
                }
                break;
            }
        }
    } else { // 0 keys in table - only when adding leaves to small initial table
        if ((rc = b.SetKey(0, key)) || (rc = b.SetVal(0, value)))
            return rc;
    }
    return b.Serialize(buffercache, node);
}


/*
 * IsNodeFull
 *
 * Tells whether a node is full or not
 */
bool BTreeIndex::IsNodeFull(const SIZE_T node)
{
    BTreeNode b;
    b.Unserialize(buffercache, node);

    switch (b.info.nodetype) {
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            return (b.info.GetNumSlotsAsInterior() == b.info.numkeys);
        case BTREE_LEAF_NODE:
            return (b.info.GetNumSlotsAsLeaf() == b.info.numkeys);
    }
    cerr << "Invalid node type passed to IsNodeFull in btree.cc" << endl;
    return false;
}


/*
 * SplitNode
 *
 * Splits any node and returns the node number for the new node, and the key
 * that should be promoted by the split
 */
ERROR_T BTreeIndex::SplitNode(const SIZE_T node, SIZE_T &newNode, KEY_T &splitKey)
{
    // in comments, n = left.info.numkeys
    BTreeNode left;
    SIZE_T keysLeft, keysRight;
    ERROR_T error;
    left.Unserialize(buffercache, node);
    BTreeNode right = left;

    if ((error = AllocateNode(newNode)))
        return error;
    if ((error = right.Serialize(buffercache, newNode)))
        return error;
    
    if (left.info.nodetype == BTREE_LEAF_NODE) {
        keysLeft = (left.info.numkeys + 2) / 2; // Ceiling of (n+1) / 2
        keysRight = left.info.numkeys - keysLeft;

        left.GetKey(keysLeft - 1, splitKey);

        // get location of first key in left/old node that will be moved
        char *src = left.ResolveKeyVal(keysLeft); 
        char *dest = right.ResolveKeyVal(0);

        memcpy(dest, src, keysRight * (left.info.keysize + left.info.valuesize));
    } else { // Root or intermediate node
        keysLeft = left.info.numkeys / 2; // Floor of n / 2
        keysRight = left.info.numkeys - keysLeft - 1; // one key will be promoted
        
        left.GetKey(keysLeft, splitKey);

        char *src = left.ResolvePtr(keysLeft + 1);
        char *dest = right.ResolvePtr(0);
        
        memcpy(dest, src, keysRight * (left.info.keysize + sizeof(SIZE_T)) + sizeof(SIZE_T));
    }
    left.info.numkeys = keysLeft;
    right.info.numkeys = keysRight;

    if ((error = left.Serialize(buffercache, node)))
        return error;
    return right.Serialize(buffercache, newNode);
}


static ERROR_T PrintNode(ostream &os, SIZE_T nodenum, BTreeNode &b, BTreeDisplayType dt)
{
    KEY_T key;
    VALUE_T value;
    SIZE_T ptr;
    SIZE_T offset;
    ERROR_T rc;
    unsigned i;
    
    if (dt==BTREE_DEPTH_DOT)
    {
        os << nodenum << " [ label=\""<<nodenum<<": ";
    }
    else if (dt==BTREE_DEPTH)
    {
        os << nodenum << ": ";
    }
    else { }
    
    switch (b.info.nodetype) {
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            if (dt==BTREE_SORTED_KEYVAL) {}
            else
            {
                if (dt==BTREE_DEPTH_DOT) {}
                else
                {
                    os << "Interior: ";
                }
                for (offset=0;offset<=b.info.numkeys;offset++)
                {
                    rc=b.GetPtr(offset,ptr);
                    if (rc) {  return rc;  }
                    os << "*" << ptr << " ";
                    // Last pointer
                    if (offset==b.info.numkeys) break;
                    rc=b.GetKey(offset,key);
                    if (rc) {  return rc; }
                    for (i=0;i<b.info.keysize;i++)
                    {
                        os << key.data[i];
                    }
                    os << " ";
                }
            }
            break;
        case BTREE_LEAF_NODE:
            if (dt==BTREE_DEPTH_DOT || dt==BTREE_SORTED_KEYVAL) {}
            else
            {
                os << "Leaf: ";
            }
            for (offset=0;offset<b.info.numkeys;offset++)
            {
                if (offset==0)
                {
                    // special case for first pointer
                    rc=b.GetPtr(offset,ptr);
                    if (rc) {  return rc;  }
                    if (dt!=BTREE_SORTED_KEYVAL)
                    {
                        os << "*" << ptr << " ";
                    }
                }
                if (dt==BTREE_SORTED_KEYVAL) {  os << "(";  }
                rc=b.GetKey(offset,key);
                
                if (rc) {  return rc; }
                for (i=0;i<b.info.keysize;i++)
                {
                    os << key.data[i];
                }
                
                if (dt==BTREE_SORTED_KEYVAL) {  os << ",";  }
                else {  os << " ";  }
                
                rc=b.GetVal(offset,value);
                if (rc) {  return rc; }
                
                for (i=0;i<b.info.valuesize;i++)
                {
                    os << value.data[i];
                }
                
                if (dt==BTREE_SORTED_KEYVAL) {  os << ")\n";  }
                else {  os << " ";  }
            }
            break;
        default:
            if (dt==BTREE_DEPTH_DOT)
            {
                os << "Unknown("<<b.info.nodetype<<")";
            }
            else
            {
                os << "Unsupported Node Type " << b.info.nodetype ;
            }
    }
    if (dt==BTREE_DEPTH_DOT) { 
        os << "\" ]";
    }
    return ERROR_NOERROR;
}


  
//
//
// DEPTH first traversal
// DOT is Depth + DOT format
//

/*
 * DisplayInternal(const SIZE_T &node, ostream &o,
                   BTreeDisplayType display_type) const
 *
 */
ERROR_T BTreeIndex::DisplayInternal(const SIZE_T &node,
                                    ostream &o,
                                    BTreeDisplayType display_type) const
{
    KEY_T testkey;
    SIZE_T ptr;
    BTreeNode b;
    ERROR_T rc;
    SIZE_T offset;
    
    rc= b.Unserialize(buffercache,node);
    
    if (rc!=ERROR_NOERROR) {
        return rc;
    }
    
    rc = PrintNode(o,node,b,display_type);
    
    if (rc) {  return rc;  }
    
    if (display_type==BTREE_DEPTH_DOT)
    {
        o << ";";
    }
    
    if (display_type!=BTREE_SORTED_KEYVAL)
    {
        o << endl;
    }
    
    switch (b.info.nodetype)
    {
        case BTREE_ROOT_NODE:
        case BTREE_INTERIOR_NODE:
            if (b.info.numkeys>0)
            {
                for (offset=0;offset<=b.info.numkeys;offset++)
                {
                    rc=b.GetPtr(offset,ptr);
                    if (rc) { return rc; }
                    if (display_type==BTREE_DEPTH_DOT)
                    {
                        o << node << " -> "<<ptr<<";\n";
                    }
                    rc=DisplayInternal(ptr,o,display_type);
                    if (rc) { return rc; }
                }
            }
            return ERROR_NOERROR;
            break;
        case BTREE_LEAF_NODE:
            return ERROR_NOERROR;
            break;
        default:
            if (display_type==BTREE_DEPTH_DOT) { 
            } else {
                o << "Unsupported Node Type " << b.info.nodetype ;
            }
            return ERROR_INSANE;
    }
    
    return ERROR_NOERROR;
}


/*
 * Name:    Print
 * Purpose:
 * Params:  ostream &os
 */
ostream & BTreeIndex::Print(ostream &os) const
{
    BTreeIndex::Display(os, BTREE_DEPTH_DOT);
    return os;
}

ostream & BTreeIndex::DebugPrint() const
{ // Because gdb has problems resolving stdout
    return Print(std::cout); 
}
