// ======================================================================== //
// Copyright 2009-2019 Intel Corporation                                    //
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

#include <vector>

#include "OSPWork.h"
#include "mpi/fb/DistributedFrameBuffer.h"
#include "mpi/render/MPILoadBalancer.h"
#include "mpiCommon/MPICommon.h"
#include "ospray/common/ObjectHandle.h"
#include "render/RenderTask.h"

#include "common/Data.h"
#include "common/Library.h"
#include "common/World.h"
#include "geometry/TriangleMesh.h"
#include "texture/Texture.h"

namespace ospray {
  namespace mpi {
    namespace work {

      void registerOSPWorkItems(WorkTypeRegistry &registry)
      {
        registerWorkUnit<SetLoadBalancer>(registry);

        registerWorkUnit<NewRenderer>(registry);
        registerWorkUnit<NewWorld>(registry);
        registerWorkUnit<NewGeometry>(registry);
        registerWorkUnit<NewGeometryInstance>(registry);
        registerWorkUnit<NewCamera>(registry);
        registerWorkUnit<NewVolume>(registry);
        registerWorkUnit<NewVolumeInstance>(registry);
        registerWorkUnit<NewTransferFunction>(registry);
        registerWorkUnit<NewPixelOp>(registry);

        registerWorkUnit<NewMaterial>(registry);
        registerWorkUnit<NewLight>(registry);

        registerWorkUnit<NewData>(registry);
        registerWorkUnit<NewTexture>(registry);

        registerWorkUnit<CommitObject>(registry);
        registerWorkUnit<CommandRelease>(registry);

        registerWorkUnit<LoadModule>(registry);

        registerWorkUnit<CreateFrameBuffer>(registry);
        registerWorkUnit<ResetAccumulation>(registry);
        registerWorkUnit<RenderFrameAsync>(registry);

        registerWorkUnit<SetRegion>(registry);

        registerWorkUnit<SetParam<OSPObject>>(registry);
        registerWorkUnit<SetParam<std::string>>(registry);
        registerWorkUnit<SetParam<int>>(registry);
        registerWorkUnit<SetParam<bool>>(registry);
        registerWorkUnit<SetParam<float>>(registry);
        registerWorkUnit<SetParam<vec2f>>(registry);
        registerWorkUnit<SetParam<vec2i>>(registry);
        registerWorkUnit<SetParam<vec3f>>(registry);
        registerWorkUnit<SetParam<vec3i>>(registry);
        registerWorkUnit<SetParam<vec4f>>(registry);
        registerWorkUnit<SetParam<vec4i>>(registry);
        registerWorkUnit<SetParam<box1f>>(registry);
        registerWorkUnit<SetParam<box2f>>(registry);
        registerWorkUnit<SetParam<box3f>>(registry);
        registerWorkUnit<SetParam<box4f>>(registry);
        registerWorkUnit<SetParam<linear3f>>(registry);
        registerWorkUnit<SetParam<affine3f>>(registry);

        registerWorkUnit<RemoveParam>(registry);

        registerWorkUnit<CommandFinalize>(registry);
        registerWorkUnit<Pick>(registry);
      }

      // SetLoadBalancer //////////////////////////////////////////////////////

      SetLoadBalancer::SetLoadBalancer(ObjectHandle _handle,
                                       bool _useDynamicLoadBalancer,
                                       int _numTilesPreAllocated)
          : useDynamicLoadBalancer(_useDynamicLoadBalancer),
            numTilesPreAllocated(_numTilesPreAllocated),
            handleID(_handle.i64)
      {
      }

      void SetLoadBalancer::run()
      {
        if (useDynamicLoadBalancer) {
          TiledLoadBalancer::instance =
              make_unique<dynamicLoadBalancer::Slave>(handleID);
        } else {
          TiledLoadBalancer::instance =
              make_unique<staticLoadBalancer::Slave>();
        }
      }

      void SetLoadBalancer::runOnMaster()
      {
        if (useDynamicLoadBalancer) {
          TiledLoadBalancer::instance =
              make_unique<dynamicLoadBalancer::Master>(handleID,
                                                       numTilesPreAllocated);
        } else {
          TiledLoadBalancer::instance =
              make_unique<staticLoadBalancer::Master>();
        }
      }

      void SetLoadBalancer::serialize(WriteStream &b) const
      {
        b << handleID << useDynamicLoadBalancer;
      }

      void SetLoadBalancer::deserialize(ReadStream &b)
      {
        b >> handleID >> useDynamicLoadBalancer;
      }

      // ospCommit ////////////////////////////////////////////////////////////

      CommitObject::CommitObject(ObjectHandle handle) : handle(handle) {}

      void CommitObject::run()
      {
        ManagedObject *obj = handle.lookup();
        if (obj) {
          obj->commit();
        } else {
          throw std::runtime_error("Error: rank " +
                                   std::to_string(mpicommon::world.rank) +
                                   " did not have object to commit!");
        }
      }

      void CommitObject::runOnMaster()
      {
        if (handle.defined()) {
          ManagedObject *obj = handle.lookup();
          if (dynamic_cast<Renderer *>(obj)) {
            obj->commit();
          }
        }
      }

      void CommitObject::serialize(WriteStream &b) const
      {
        b << (int64)handle;
      }

      void CommitObject::deserialize(ReadStream &b)
      {
        b >> handle.i64;
      }

      // ospNewFrameBuffer ////////////////////////////////////////////////////

      CreateFrameBuffer::CreateFrameBuffer(ObjectHandle handle,
                                           vec2i dimensions,
                                           OSPFrameBufferFormat format,
                                           uint32 channels)
          : handle(handle),
            dimensions(dimensions),
            format(format),
            channels(channels)
      {
      }

      void CreateFrameBuffer::run()
      {
        assert(dimensions.x > 0);
        assert(dimensions.y > 0);

        FrameBuffer *fb =
            new DistributedFrameBuffer(dimensions, handle, format, channels);
        handle.assign(fb);
      }

      void CreateFrameBuffer::runOnMaster()
      {
        run();
      }

      void CreateFrameBuffer::serialize(WriteStream &b) const
      {
        b << (int64)handle << dimensions << (int32)format << channels;
      }

      void CreateFrameBuffer::deserialize(ReadStream &b)
      {
        int32 fmt;
        b >> handle.i64 >> dimensions >> fmt >> channels;
        format = (OSPFrameBufferFormat)fmt;
      }

      // ospLoadModule ////////////////////////////////////////////////////////

      LoadModule::LoadModule(const std::string &name) : name(name) {}

      void LoadModule::run()
      {
        errorCode = loadLocalModule(name);
      }

      void LoadModule::runOnMaster()
      {
        run();
      }

      void LoadModule::serialize(WriteStream &b) const
      {
        b << name;
      }

      void LoadModule::deserialize(ReadStream &b)
      {
        b >> name;
      }

      // ospSetParam //////////////////////////////////////////////////////////

      template <>
      void SetParam<std::string>::run()
      {
        ManagedObject *obj = handle.lookup();
        Assert(obj);
        obj->setParam(name, val);
      }

      template <>
      void SetParam<std::string>::runOnMaster()
      {
        if (!handle.defined())
          return;

        ManagedObject *obj = handle.lookup();
        if (dynamic_cast<Renderer *>(obj) || dynamic_cast<Volume *>(obj)) {
          obj->setParam(name, val);
        }
      }

      // ospNewRenderer ///////////////////////////////////////////////////////

      template <>
      void NewRenderer::runOnMaster()
      {
        run();
      }

      // ospNewVolume /////////////////////////////////////////////////////////

      template <>
      void NewVolume::runOnMaster()
      {
        run();
      }

      // ospNewWorld //////////////////////////////////////////////////////////

      template <>
      void NewWorld::run()
      {
        auto *world = new World;
        handle.assign(world);
      }

      // ospNewMaterial ///////////////////////////////////////////////////////

      void NewMaterial::run()
      {
        auto *material = Material::createInstance(rendererType.c_str(),
                                                  materialType.c_str());
        handle.assign(material);
      }

      // ospNewGeometryInstance ///////////////////////////////////////////////

      void NewGeometryInstance::run()
      {
        auto *geom     = (Geometry *)geometryHandle.lookup();
        auto *instance = new GeometryInstance(geom);
        handle.assign(instance);
      }

      // ospNewVolumeInstance ///////////////////////////////////////////////

      void NewVolumeInstance::run()
      {
        auto *geom     = (Volume *)volumeHandle.lookup();
        auto *instance = new VolumeInstance(geom);
        handle.assign(instance);
      }

      // ospNewLight //////////////////////////////////////////////////////////

      void NewLight::run()
      {
        auto *material = Light::createInstance(type.c_str());
        handle.assign(material);
      }

      // ospNewData ///////////////////////////////////////////////////////////

      NewData::NewData(ObjectHandle handle,
                       size_t nItems,
                       OSPDataType format,
                       const void *_initMem,
                       int flags)
          : handle(handle), nItems(nItems), format(format), flags(flags)
      {
        if (_initMem && nItems) {
          auto numBytes = sizeOf(format) * nItems;

          auto *initMem = static_cast<const byte_t *>(_initMem);

          if (flags & OSP_DATA_SHARED_BUFFER) {
            dataView.reset(const_cast<byte_t *>(initMem), numBytes);
          } else {
            copiedData.resize(numBytes);
            std::memcpy(copiedData.data(), initMem, numBytes);
            dataView = copiedData;
          }
        }
      }

      void NewData::run()
      {
        // iw - not sure if string would be handled correctly (I doubt
        // it), so let's assert that nobody accidentally uses it.
        assert(format != OSP_STRING);

        if (format == OSP_OBJECT || format == OSP_CAMERA ||
            format == OSP_DATA || format == OSP_FRAMEBUFFER ||
            format == OSP_GEOMETRY || format == OSP_LIGHT ||
            format == OSP_MATERIAL || format == OSP_WORLD ||
            format == OSP_RENDERER || format == OSP_TEXTURE ||
            format == OSP_TRANSFER_FUNCTION || format == OSP_VOLUME ||
            format == OSP_PIXEL_OP) {
          /* translating handles to managedobject pointers: if a
             data array has 'object' or 'data' entry types, then
             what the host sends are _handles_, not pointers, but
             what the core expects are pointers; to make the core
             happy we translate all data items back to pointers at
             this stage */
          ObjectHandle *asHandle   = (ObjectHandle *)dataView.data();
          ManagedObject **asObjPtr = (ManagedObject **)dataView.data();
          for (size_t i = 0; i < nItems; ++i) {
            if (asHandle[i] != NULL_HANDLE)
              asObjPtr[i] = asHandle[i].lookup();
          }
        }

        Data *ospdata = new Data(nItems, format, dataView.data());
        handle.assign(ospdata);
      }

      void NewData::serialize(WriteStream &b) const
      {
        b << (int64)handle << nItems << (int32)format << flags << dataView;
      }

      void NewData::deserialize(ReadStream &b)
      {
        int32 fmt;
        b >> handle.i64 >> nItems >> fmt >> flags >> copiedData;
        dataView = copiedData;
        format   = (OSPDataType)fmt;
      }

      // ospSetRegion /////////////////////////////////////////////////////////

      SetRegion::SetRegion(OSPVolume volume,
                           vec3i start,
                           vec3i size,
                           const void *src,
                           OSPDataType type)
          : handle((ObjectHandle &)volume),
            regionStart(start),
            regionSize(size),
            type(type)
      {
        size_t bytes = ospray::sizeOf(type) * size.x * size.y * size.z;
        // TODO: With the MPI batching this limitation should be lifted
        if (bytes > 2000000000LL) {
          throw std::runtime_error(
              "MPI ospSetRegion does not support "
              "region sizes > 2GB");
        }
        data.resize(bytes);

        // TODO: should support sending data without copy
        std::memcpy(data.data(), src, bytes);
      }

      void SetRegion::run()
      {
        Volume *volume = (Volume *)handle.lookup();
        Assert(volume);
        // TODO: Does it make sense to do the allreduce & report back fails?
        // TODO: Should we be allocating the data with alignedMalloc instead?
        // We could use a std::vector with an aligned std::allocator
        if (!volume->setRegion(data.data(), regionStart, regionSize)) {
          throw std::runtime_error("Failed to set region for volume");
        }
      }

      void SetRegion::serialize(WriteStream &b) const
      {
        b << (int64)handle << regionStart << regionSize << (int32)type << data;
      }

      void SetRegion::deserialize(ReadStream &b)
      {
        int32 ty;
        b >> handle.i64 >> regionStart >> regionSize >> ty >> data;
        type = (OSPDataType)ty;
      }

      // ospResetAccumulation /////////////////////////////////////////////////

      ResetAccumulation::ResetAccumulation(OSPFrameBuffer fb)
          : handle((ObjectHandle &)fb)
      {
      }

      void ResetAccumulation::run()
      {
        FrameBuffer *fb = (FrameBuffer *)handle.lookup();
        Assert(fb);
        fb->clear();
      }

      void ResetAccumulation::runOnMaster()
      {
        run();
      }

      void ResetAccumulation::serialize(WriteStream &b) const
      {
        b << (int64)handle;
      }

      void ResetAccumulation::deserialize(ReadStream &b)
      {
        b >> handle.i64;
      }

      // ospRenderFrameAsync //////////////////////////////////////////////////

      RenderFrameAsync::RenderFrameAsync(OSPFrameBuffer fb,
                                         OSPRenderer renderer,
                                         OSPCamera camera,
                                         OSPWorld world,
                                         ObjectHandle futureHandle)
          : fbHandle((ObjectHandle &)fb),
            rendererHandle((ObjectHandle &)renderer),
            cameraHandle((ObjectHandle &)camera),
            worldHandle((ObjectHandle &)world),
            futureHandle(futureHandle)
      {
      }

      void RenderFrameAsync::run()
      {
        mpicommon::world.barrier();
        Renderer *renderer = (Renderer *)rendererHandle.lookup();
        FrameBuffer *fb    = (FrameBuffer *)fbHandle.lookup();
        Camera *camera     = (Camera *)cameraHandle.lookup();
        World *world       = (World *)worldHandle.lookup();

        fb->setCompletedEvent(OSP_NONE_FINISHED);

        fb->refInc();
        renderer->refInc();
        camera->refInc();
        world->refInc();

        auto *f = new RenderTask(fb, [=]() {
          float result = renderer->renderFrame(fb, camera, world);

          fb->refDec();
          renderer->refDec();
          camera->refDec();
          world->refDec();

          return result;
        });

        futureHandle.assign(f);
      }

      void RenderFrameAsync::runOnMaster()
      {
        mpicommon::world.barrier();
        Renderer *renderer = (Renderer *)rendererHandle.lookup();
        FrameBuffer *fb    = (FrameBuffer *)fbHandle.lookup();

        fb->setCompletedEvent(OSP_NONE_FINISHED);

        fb->refInc();
        renderer->refInc();

        // The master doesn't have or need a world or camera, so skip
        // looking them up
        auto *f = new RenderTask(fb, [=]() {
          float result = renderer->renderFrame(fb, nullptr, nullptr);

          fb->refDec();
          renderer->refDec();

          return result;
        });

        futureHandle.assign(f);
      }

      void RenderFrameAsync::serialize(WriteStream &b) const
      {
        b << (int64)fbHandle << (int64)rendererHandle << (int64)cameraHandle
          << (int64)worldHandle << (int64)futureHandle;
      }

      void RenderFrameAsync::deserialize(ReadStream &b)
      {
        b >> fbHandle.i64 >> rendererHandle.i64 >> cameraHandle.i64 >>
            worldHandle.i64 >> futureHandle.i64;
      }

      // ospRemoveParam ///////////////////////////////////////////////////////

      RemoveParam::RemoveParam(ObjectHandle handle, const char *name)
          : handle(handle), name(name)
      {
        Assert(handle != nullHandle);
      }

      void RemoveParam::run()
      {
        ManagedObject *obj = handle.lookup();
        Assert(obj);
        obj->removeParam(name.c_str());
      }

      void RemoveParam::runOnMaster()
      {
        ManagedObject *obj = handle.lookup();
        if (dynamic_cast<Renderer *>(obj) || dynamic_cast<Volume *>(obj)) {
          obj->removeParam(name.c_str());
        }
      }

      void RemoveParam::serialize(WriteStream &b) const
      {
        b << (int64)handle << name;
      }

      void RemoveParam::deserialize(ReadStream &b)
      {
        b >> handle.i64 >> name;
      }

      // ospRelease ///////////////////////////////////////////////////////////

      CommandRelease::CommandRelease(ObjectHandle handle) : handle(handle) {}

      void CommandRelease::run()
      {
        handle.freeObject();
      }

      void CommandRelease::runOnMaster()
      {
        // master only create some type of objects
        if (handle.defined())
          handle.freeObject();
      }

      void CommandRelease::serialize(WriteStream &b) const
      {
        b << (int64)handle;
      }

      void CommandRelease::deserialize(ReadStream &b)
      {
        b >> handle.i64;
      }

      // ospFinalize //////////////////////////////////////////////////////////

      void CommandFinalize::run()
      {
        runOnMaster();

        // TODO: Is it ok to call exit again here?
        // should we be calling exit? When the MPIDevice is
        // destroyed (at program exit) we'll send this command
        // to ourselves/other ranks. In master/worker mode
        // the workers should call std::exit to leave the worker loop
        // but the master or all ranks in collab mode would already
        // be exiting.
        std::exit(0);
      }

      void CommandFinalize::runOnMaster()
      {
        maml::shutdown();
        MPI_CALL(Finalize());
      }

      void CommandFinalize::serialize(WriteStream &) const {}

      void CommandFinalize::deserialize(ReadStream &) {}

      // ospPick //////////////////////////////////////////////////////////////

      Pick::Pick(OSPFrameBuffer fb,
                 OSPRenderer renderer,
                 OSPCamera camera,
                 OSPWorld world,
                 const vec2f &screenPos)
          : fbHandle((ObjectHandle &)fb),
            rendererHandle((ObjectHandle &)renderer),
            cameraHandle((ObjectHandle &)camera),
            worldHandle((ObjectHandle &)world),
            screenPos(screenPos)
      {
      }

      void Pick::run()
      {
        // The MPIOffloadDevice only handles duplicated data, so
        // just have the first worker run the pick and send the result
        // back to the master
        if (mpicommon::world.rank == 1) {
          FrameBuffer *fb    = (FrameBuffer *)fbHandle.lookup();
          Renderer *renderer = (Renderer *)rendererHandle.lookup();
          Camera *camera     = (Camera *)cameraHandle.lookup();
          World *world       = (World *)worldHandle.lookup();

          pickResult = renderer->pick(fb, camera, world, screenPos);

          mpicommon::send(&pickResult,
                          sizeof(pickResult),
                          MPI_BYTE,
                          0,
                          typeIdOf<Pick>(),
                          mpicommon::world.comm)
              .wait();
        }
        mpicommon::barrier(mpicommon::worker.comm).wait();
      }
      void Pick::runOnMaster()
      {
        // Master just needs to recv the result from the first worker
        mpicommon::recv(&pickResult,
                        sizeof(pickResult),
                        MPI_BYTE,
                        1,
                        typeIdOf<Pick>(),
                        mpicommon::world.comm)
            .wait();
      }

      void Pick::serialize(WriteStream &b) const
      {
        b << (int64)fbHandle << (int64)rendererHandle << (int64)cameraHandle
          << (int64)worldHandle << screenPos;
      }

      void Pick::deserialize(ReadStream &b)
      {
        b >> fbHandle.i64 >> rendererHandle.i64 >> cameraHandle.i64 >>
            worldHandle.i64 >> screenPos;
      }

    }  // namespace work
  }    // namespace mpi
}  // namespace ospray
