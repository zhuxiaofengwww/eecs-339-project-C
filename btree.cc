/* --------------------------------------------------------------------------
 * Project C - BTree Index
 * EECS 339, Fall 2012, Prof. Dinda
 *
 *
 * Lizz Bartos - eab879
 * Stephen Duranski -
 * Alex Jablonski - 
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
    

/*
 * Detach(SIZE_T &initblock)
 *
 */
ERROR_T BTreeIndex::Detach(SIZE_T &initblock)
{
  return superblock.Serialize(buffercache,superblock_index);
}
 
/*
 * PlaceKeyVal
 *
 * Looks up the node where a key should go
 */
ERROR_T BTreeIndex::PlaceKeyVal(SIZE_T &node, SIZE_T &parent, const KEY_T &key, const VALUE_T &value)
{
    // Make sure not to pass superblock.info.rootnode as the 1st parameter
    // because that's the sort of thing we don't want to change
    BTreeNode b;
    ERROR_T rc;
    SIZE_T offset;
    KEY_T testkey;
    SIZE_T ptr;
    SIZE_T entriesToCopy;
    SIZE_T numkeys;

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
                    node = ptr;
                    return PlaceKeyVal(node, parent, key, value);
                }
            }
            // if we got here, we need to go to the next pointer, if it exists
            if (b.info.numkeys>0)
            {
                rc=b.GetPtr(b.info.numkeys,ptr);
                if (rc) { return rc; }
                node = ptr;
                return PlaceKeyVal(node, parent, key, value);
            }
            else {
                // There are no keys at all on this node, so nowhere to go
                return ERROR_NONEXISTENT;
            }
            break;
            
        // Leaf nodes: store keys and their associated values
        case BTREE_LEAF_NODE:
            // Scan through keys looking for matching value
            numkeys = b.info.numkeys++; // This has to be done before setting keys/vals for the
                                        // last element, and is done in any case anyway
            if (numkeys == b.info.GetNumSlotsAsLeaf()) {
                return ERROR_NOSPACE; // UNIMPLEMENTED: handling full leaves
            } else if (numkeys > 0) {
                for (offset=0, entriesToCopy = numkeys;
                        offset < numkeys;
                        offset++, entriesToCopy--) {
                    if ((rc = b.GetKey(offset, testkey)))
                        return rc;
                    // If the new key is less than the current key, shift all greater keys up
                    // and put this new key in the current location
                    if (key < testkey) {
                        void *src = b.ResolveKeyVal(offset);
                        void *dest = b.ResolveKeyVal(offset + 1);
                        memmove(dest, src, entriesToCopy * (b.info.keysize + b.info.valuesize));
                        if ((rc = b.SetKey(offset, key)) ||
                            (rc = b.SetVal(offset, value)))
                            return rc;
                        break;
                    }
                    if (offset == numkeys - 1) { 
                        // If we are on the last key, and the new key was not less than this one, 
                        // then the new key becomes the last key
                        if ((rc = b.SetKey(numkeys, key)) ||
                            (rc = b.SetVal(numkeys, value)))
                            return rc;
                        break;
                    }
                }
            } else { // 0 keys in table
                if ((rc = b.SetKey(0, key)) ||
                    (rc = b.SetVal(0, value)))
                    return rc;
            }
            return b.Serialize(buffercache, node); // Write any changes made
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

/*
 * Name:    Lookup
 * Purpose: return the value associated with the key
 * Params:  const KEY_T &key,
 *          VALUE_T &value
 * Returns: 
 */
ERROR_T BTreeIndex::Lookup(const KEY_T &key, VALUE_T &value)
{
    return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_LOOKUP, key, value);
}

/*
 * Name:    Insert
 * Purpose: insert the key/value pair
 * Params:  const KEY_T &key,
 *          const VALUE_T &value
 * Returns: 
 * TODO................................................................
 */
ERROR_T BTreeIndex::Insert(const KEY_T &key, const VALUE_T &value)
{
    // Insertion of existing keys should fail (update is the appropriate operation)

    BTreeNode root;
    root.Unserialize(buffercache,superblock.info.rootnode);

    if (root.info.numkeys == 0) { // This is the case when root is empty
        BTreeNode leaf(BTREE_LEAF_NODE, 
            superblock.info.keysize,
            superblock.info.valuesize,
            buffercache->GetBlockSize());
        
        SIZE_T leftNode;
        SIZE_T rightNode;
        ERROR_T error;
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

    SIZE_T node = superblock.info.rootnode; 
    SIZE_T parentNode = node;
    VALUE_T temp;

    if (ERROR_NONEXISTENT == Lookup(key, temp))
        return PlaceKeyVal(node, parentNode, key, value);
    else
        return ERROR_CONFLICT;
}


/*
 * Name:    Update
 * Purpose: change the value associated with an existing key
 * Params:  const KEY_T &key
 *          const VALUE_T &value
 * Returns:
 * TODO................................................................
 */
ERROR_T BTreeIndex::Update(const KEY_T &key, const VALUE_T &value)
{
    // WROTE ME
    VALUE_T valueWritable = value;
    return LookupOrUpdateInternal(superblock.info.rootnode, BTREE_OP_UPDATE, key, valueWritable);
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
 * Name:    Print
 * Purpose:
 * Params:  ostream &os
 *
 * TODO................................................................
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
