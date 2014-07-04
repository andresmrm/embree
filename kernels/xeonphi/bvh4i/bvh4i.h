// ======================================================================== //
// Copyright 2009-2014 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "common/alloc.h"
#include "common/accel.h"
#include "common/scene.h"
#include "geometry/primitive.h"
#include "geometry/triangle1.h"

namespace embree
{
  /*! Multi BVH with 4 children. Each node stores the bounding box of
   * it's 4 children as well as a 4 child indices. */
  class BVH4i : public Bounded
  {
  public:

    /*! forward declaration of node type */
    struct Node;

    /*! branching width of the tree */
    static const size_t N = 4;

    /*! Masks the bits that store the number of items per leaf. */
    static const unsigned int encodingBits = 4;
    static const unsigned int offset_mask  = 0xFFFFFFFF << encodingBits;
    static const unsigned int leaf_shift   = 3;
    static const unsigned int leaf_mask    = 1<<leaf_shift;  
    static const unsigned int items_mask   = leaf_mask-1;  
    
    /*! Empty node */
    static const unsigned int emptyNode = leaf_mask;

    /*! Invalid node */
    static const unsigned int invalidNode = (unsigned int)-1;

    /*! Maximal depth of the BVH. */
    static const size_t maxBuildDepth = 26;
    static const size_t maxBuildDepthLeaf = maxBuildDepth+6;
    static const size_t maxDepth = maxBuildDepth + maxBuildDepthLeaf;
    
    /*! Cost of one traversal step. */
    static const int travCost = 1;

    static const size_t hybridSIMDUtilSwitchThreshold = 7;

    /*! References a Node or list of Triangles */
    struct NodeRef
    {
      /*! Default constructor */
      __forceinline NodeRef () {}

      /*! Construction from integer */
      __forceinline NodeRef (unsigned id) : _id(id) { }

      /*! Cast to unsigned */
      __forceinline operator unsigned int() const { return _id; }
     
      /*! checks if this is a leaf */
      __forceinline unsigned int isLeaf() const { return _id & leaf_mask; }

      /*! checks if this is a leaf */
      __forceinline unsigned int isLeaf(const unsigned int mask) const { return _id & mask; }
      
      /*! checks if this is a node */
      __forceinline unsigned isNode() const { return (_id & leaf_mask) == 0; }
      
      /*! returns node pointer */

      // use free adressing mode: lea reg,reg*2 as adressing is done with respect to 32 bytes blocks
      __forceinline       Node* node(      void* base) const { return (      Node*)((      short*)base + (size_t)_id); }
      __forceinline const Node* node(const void* base) const { return (const Node*)((const short*)base + (size_t)_id); }
      

      __forceinline unsigned int nodeID() const { return (_id*2) / sizeof(Node);  }
      
      /*! returns leaf pointer */
      template<unsigned int scale=4>
      __forceinline const char* leaf(const void* base, unsigned int& num) const {
        assert(isLeaf());
        num = _id & items_mask;
        return (const char*)base + (_id & offset_mask)*scale;
      }

      /*! returns leaf pointer */
      template<unsigned int scale=4>
      __forceinline const char* leaf(const void* base) const {
        assert(isLeaf());
        return (const char*)base + (_id & offset_mask)*scale;
      }

      __forceinline unsigned int offset() const {
        return _id & offset_mask;
      }

      __forceinline unsigned int offsetIndex() const {
        return _id >> encodingBits;
      }

      __forceinline unsigned int items() const {
        return _id & items_mask;
      }
      
      __forceinline unsigned int &id() { return _id; }
    private:
      unsigned int _id;
    };

    /*! BVH4i Node */

    struct __aligned(64) Node
    {
    public:
      struct NodeStruct {
        float x,y,z;           // x,y, and z coordinates of bounds
        NodeRef child;         
      } lower[4], upper[4];    

      /*! Returns bounds of specified child. */
      __forceinline BBox3fa bounds(size_t i) const {
	assert( i < 4 );
        Vec3fa l = *(Vec3fa*)&lower[i];
        Vec3fa u = *(Vec3fa*)&upper[i];
        return BBox3fa(l,u);
      }

      __forceinline void setBounds(size_t i, const BBox3fa &b) {
	assert( i < 4 );
	lower[i].x = b.lower.x;
	lower[i].y = b.lower.y;
	lower[i].z = b.lower.z;

	upper[i].x = b.upper.x;
	upper[i].y = b.upper.y;
	upper[i].z = b.upper.z;
      }

      __forceinline mic_f lowerXYZ(size_t i) const {
	return broadcast4to16f(&lower[i]);
      }

      __forceinline mic_f upperXYZ(size_t i) const {
	return broadcast4to16f(&upper[i]);
      }

      __forceinline bool isPoint(size_t i) const {
	mic_m m_lane = ((unsigned int)0x7) << (4*i);
	mic_m m_box  = eq(m_lane,load16f(lower),load16f(upper));
	return (unsigned int)m_box == (unsigned int)m_lane;
      }

      __forceinline void setInvalid(size_t i) 
      {
	lower[i].x = pos_inf;
	lower[i].y = pos_inf;
	lower[i].z = pos_inf;
	lower[i].child = invalidNode; 

	upper[i].x = neg_inf;
	upper[i].y = neg_inf;
	upper[i].z = neg_inf;
	upper[i].child = NodeRef(0);
      }

      /*! Returns reference to specified child */
      __forceinline       NodeRef& child(size_t i)       { return lower[i].child; }
      __forceinline const NodeRef& child(size_t i) const { return lower[i].child; }


      __forceinline std::ostream& operator<<(std::ostream &o)
      {
	for (size_t i=0;i<4;i++)
	  {
	    o << "lower: [" << lower[i].x << "," << lower[i].y << "," << lower[i].z << "] ";
	    o << "upper: [" << upper[i].x << "," << upper[i].y << "," << upper[i].z << "] ";
	    o << std::endl;
	  }
	return o;
      }
    };


    /*! 64bit byte-quantized BVH4i Node */

    struct __aligned(64) QuantizedNode
    {
    public:
      Vec3f start;
      NodeRef child0;
      Vec3f diff;
      NodeRef child1;
      unsigned char lower[12];
      NodeRef child2;
      unsigned char upper[12];
      NodeRef child3;

      /*! Returns reference to specified child */
      __forceinline       NodeRef &child(size_t i)       { return ((NodeRef*)this)[3+4*i]; }
      __forceinline const NodeRef &child(size_t i) const { return ((NodeRef*)this)[3+4*i]; }

      __forceinline mic_f lowerXYZ() const
      {
	return uload16f_low_uint8(0x7777,lower,mic_f::zero());
      }

      __forceinline mic_f decompress_lowerXYZ(const mic_f &s, const mic_f &d)  const
      {
	return s + d * lowerXYZ();
      }

      __forceinline mic_f upperXYZ()  const
      {
	return uload16f_low_uint8(0x7777,upper,mic_f::zero());
      }

      __forceinline bool isPoint(size_t i) const {
	mic_m m_lane = ((unsigned int)0x7) << (4*i);
	mic_m m_box  = eq(m_lane,lowerXYZ(),upperXYZ());
	return (unsigned int)m_box == (unsigned int)m_lane;
      }

      __forceinline BBox3fa bounds(size_t i) const {
	assert( i < 4 );
	const mic_f s = decompress_startXYZ();
	const mic_f d = decompress_diffXYZ();

	const mic_f decompress_lower_XYZ = decompress_lowerXYZ(s,d);
	const mic_f decompress_upper_XYZ = decompress_upperXYZ(s,d);

        Vec3fa l = ((Vec3fa*)&decompress_lower_XYZ)[i];
        Vec3fa u = ((Vec3fa*)&decompress_upper_XYZ)[i];
        return BBox3fa(l,u);
      }

      __forceinline mic_f decompress_upperXYZ(const mic_f &s, const mic_f &d)  const
      {
	return s + d * upperXYZ();
      }

      __forceinline mic_f decompress_startXYZ() const
      {
	return broadcast4to16f(&start);
      }

      __forceinline mic_f decompress_diffXYZ() const
      {
	return broadcast4to16f(&diff);
      }

      __forceinline void init( const Node &node) 
      {
	mic_f l0 = node.lowerXYZ(0);
	mic_f l1 = node.lowerXYZ(1);
	mic_f l2 = node.lowerXYZ(2);
	mic_f l3 = node.lowerXYZ(3);

	mic_f u0 = node.upperXYZ(0);
	mic_f u1 = node.upperXYZ(1);
	mic_f u2 = node.upperXYZ(2);
	mic_f u3 = node.upperXYZ(3);

	const mic_f minXYZ = select(0x7777,min(min(l0,l1),min(l2,l3)),mic_f::zero());
	const mic_f maxXYZ = select(0x7777,max(max(u0,u1),max(u2,u3)),mic_f::one());
	const mic_f diffXYZ = maxXYZ - minXYZ;

	const mic_f rcp_diffXYZ = mic_f(255.0f) / diffXYZ;
 
	const mic_f nlower = load16f(node.lower);
	const mic_f nupper = load16f(node.upper);
	const mic_m isInvalid = eq(0x7777,nlower,pos_inf);

	const mic_f node_lowerXYZ = select(mic_m(0x7777) ^ isInvalid,nlower,minXYZ); 
	const mic_f node_upperXYZ = select(mic_m(0x7777) ^ isInvalid,nupper,minXYZ); 

	mic_f local_lowerXYZ = ((node_lowerXYZ - minXYZ) * rcp_diffXYZ) - 0.5f;
	mic_f local_upperXYZ = ((node_upperXYZ - minXYZ) * rcp_diffXYZ) + 0.5f;

	store4f(&start,minXYZ);
	store4f(&diff ,diffXYZ * (1.0f/255.0f));
	compactustore16f_low_uint8(0x7777,lower,local_lowerXYZ);
	compactustore16f_low_uint8(0x7777,upper,local_upperXYZ);

	child0 = node.child(0);
	child1 = node.child(1);
	child2 = node.child(2);
	child3 = node.child(3);

#if DEBUG

	const mic_f s = decompress_startXYZ();
	const mic_f d = decompress_diffXYZ();

	const mic_f decompress_lower_XYZ = decompress_lowerXYZ(s,d);
	const mic_f decompress_upper_XYZ = decompress_upperXYZ(s,d);

	if ( any(gt(0x7777,decompress_lower_XYZ,node_lowerXYZ)) ) 
	   { 
	     DBG_PRINT(node_lowerXYZ);  
	     DBG_PRINT(decompress_lower_XYZ);  
	   } 

	if ( any(lt(0x7777,decompress_upper_XYZ,node_upperXYZ)) )
	  {
	    DBG_PRINT(node_upperXYZ);
	    DBG_PRINT(decompress_upper_XYZ);
	  }
#endif
      }

    };

  public:

    /*! BVH4 default constructor. */
    BVH4i (const PrimitiveType& primTy, void* geometry = NULL)
      : primTy(primTy), 
      geometry(geometry), 
      root(emptyNode), 
      qbvh(NULL), 
      accel(NULL),
      size_node(0),
      size_accel(0)
    {
    }

    ~BVH4i();

    /*! BVH4i instantiations */
    static Accel* BVH4iTriangle1ObjectSplitBinnedSAH(Scene* scene);
    static Accel* BVH4iTriangle1ObjectSplitMorton(Scene* scene);
    static Accel* BVH4iTriangle1ObjectSplitEnhancedMorton(Scene* scene);
    static Accel* BVH4iTriangle1PreSplitsBinnedSAH(Scene* scene);
    static Accel* BVH4iVirtualGeometryBinnedSAH(Scene* scene);
    static Accel* BVH4iTriangle1MemoryConservativeBinnedSAH(Scene* scene);
    static Accel* BVH4iTriangle1ObjectSplitMorton64Bit(Scene* scene);

    /*! Calculates the SAH of the BVH */
    float sah ();

    /*! Data of the BVH */
  public:
    NodeRef root;                  //!< Root node (can also be a leaf).

    const PrimitiveType& primTy;   //!< triangle type stored in BVH
    void* geometry;                //!< pointer to geometry for intersection

 /*! Memory allocation */
  public:

    __forceinline       void* nodePtr()       { return qbvh; }
    __forceinline const void* nodePtr() const { return qbvh; }

    __forceinline       void* triPtr()       { return accel; }
    __forceinline const void* triPtr() const { return accel; }


    size_t bytes () const {
      return size_node+size_accel;
    }

    size_t size_node;
    size_t size_accel;

    Node *qbvh;
    void *accel;


    struct Helper { float x,y,z; int a; }; 

    static Helper initQBVHNode[4];

  private:
    float sah (NodeRef& node, BBox3fa bounds);
  };


  __forceinline std::ostream &operator<<(std::ostream &o, const BVH4i::Node &v)
    {
      o << std::endl;
      o << "lower: ";
      for (size_t i=0;i<4;i++) o << "[" << v.lower[i].x << "," << v.lower[i].y << "," << v.lower[i].z << "," << v.lower[i].child <<"] ";
      o << std::endl;
      o << "upper: ";
      for (size_t i=0;i<4;i++) o << "[" << v.upper[i].x << "," << v.upper[i].y << "," << v.upper[i].z << "," << v.upper[i].child <<"] ";
      o << std::endl;
      return o;
    } 

  __forceinline std::ostream& operator<<(std::ostream &o, BVH4i::QuantizedNode &v)
    {
      o << "start " << v.start << " diff " << v.diff << std::endl;
      o << "lower " << v.decompress_lowerXYZ(v.decompress_startXYZ(),v.decompress_diffXYZ()) << std::endl;
      o << "upper " << v.decompress_upperXYZ(v.decompress_startXYZ(),v.decompress_diffXYZ()) << std::endl;
      o << "child0 " << v.child(0).nodeID() << " child1 " << v.child(1).nodeID() << " child2 " << v.child(2).nodeID() << " child3 " << v.child(3).nodeID() << std::endl;
      return o;
    }



  __forceinline mic_f initTriangle1(const mic_f &v0,
				    const mic_f &v1,
				    const mic_f &v2,
				    const mic_i &geomID,
				    const mic_i &primID,
				    const mic_i &mask)
  {
    const mic_f e1 = v0 - v1;
    const mic_f e2 = v2 - v0;	     
    const mic_f normal = lcross_xyz(e1,e2);
    const mic_f _v0 = select(0x8888,cast((__m512i)primID),v0);
    const mic_f _v1 = select(0x8888,cast((__m512i)geomID),v1);
    const mic_f _v2 = select(0x8888,cast((__m512i)mask),v2);
    const mic_f _v3 = select(0x8888,mic_f::zero(),normal);
    const mic_f final = lane_shuffle_gather<0>(_v0,_v1,_v2,_v3);
    return final;
  }





}
