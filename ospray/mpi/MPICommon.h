// ======================================================================== //
// Copyright 2009-2016 Intel Corporation                                    //
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

#include <mpi.h>
#include "common/OSPCommon.h"
#include "mpi/Work.h"

// IMPI on Windows defines MPI_CALL already, erroneously
#ifdef MPI_CALL
# undef MPI_CALL
#endif
/*! helper macro that checks the return value of all MPI_xxx(...)
    calls via MPI_CALL(xxx(...)).  */
#define MPI_CALL(a) { int rc = MPI_##a; if (rc != MPI_SUCCESS) throw std::runtime_error("MPI call returned error"); }

namespace ospray {

  /*! Helper class for MPI Programming */
  namespace mpi {

    inline void checkMpiError(int rc)
    {
      if (rc != MPI_SUCCESS)
        throw std::runtime_error("MPI Error");
    }

    //! abstraction for an MPI group. 
    /*! it's the responsiblity of the respective mpi setup routines to
      fill in the proper values */
    struct Group {
      /*! whether the current process/thread is a member of this
        gorup */
      bool containsMe {false};
      /*! communictor for this group. intercommunicator if i'm a
        member of this gorup; else it's an intracommunicator */
      MPI_Comm comm {MPI_COMM_NULL};
      /*! my rank in this group if i'm a member; else set to
        MPI_ROOT */
      int rank {-1};
      /*! size of this group if i'm a member, else size of remote
        group this intracommunicaotr refers to */
      int size {-1};

#if 1
      // this is the RIGHT naming convention - old code has them all inside out.
      void makeIntraComm() 
      { MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&size); containsMe = true; }
      void makeIntraComm(MPI_Comm comm)
      { this->comm = comm; makeIntraComm(); }
      void makeInterComm(MPI_Comm comm)
      { this->comm = comm; makeInterComm(); }
      void makeInterComm()
      { containsMe = false; rank = MPI_ROOT; MPI_Comm_remote_size(comm,&size); }
#else
      void makeIntercomm() 
      { MPI_Comm_rank(comm,&rank); MPI_Comm_size(comm,&size); containsMe = true; }
      void makeIntracomm(MPI_Comm comm)
      { this->comm = comm; makeIntracomm(); }
      void makeIntracomm()
      { containsMe = false; rank = MPI_ROOT; MPI_Comm_remote_size(comm,&size); }
#endif

      /*! perform a MPI_barrier on this communicator */
      void barrier() { MPI_CALL(Barrier(comm)); }
    };

    // //! abstraction for any other peer node that we might want to communicate with
    struct Address {
      //! group that this peer is in
      Group *group;
      //! this peer's rank in this group
      int32  rank;
        
      Address(Group *group=NULL, int32 rank=-1)
        : group(group), rank(rank)
      {}
      inline bool isValid() const { return group != NULL && rank >= 0; }
    };

    //special flags for sending and reciving from all ranks instead of individuals
    const int32 SEND_ALL=-1;
    const int32 RECV_ALL=-1;

    //     //! abstraction for any other peer node that we might want to communicate with
    // struct Address {
    //   //! group that this peer is in
    //   Group *group;
    //   //! this peer's rank in this group
    //   int32  rank;
        
    //   Address(Group *group=NULL, int32 rank=-1)
    //     : group(group), rank(rank)
    //   {}
    //   inline bool isValid() const { return group != NULL && rank >= 0; }
    // };

    // struct Message {
    //     Address     addr;
    //     void       *ptr;
    //     int32       size;
    //     MPI_Request request; //! request for MPI_Test
    //     int         done;    //! done flag for MPI_Test
    //     MPI_Status  status;  //! status for MPI_Test
    // };

    // OSPRAY_INTERFACE void send(const Address& address, void* msgPtr, int32 msgSize);
    OSPRAY_INTERFACE void send(const Address& addr, work::Work* work);
    OSPRAY_INTERFACE void recv(const Address& addr, std::vector<work::Work*>& work);  //TODO: callback?
    // OSPRAY_INTERFACE void send(const Address& addr, )
    OSPRAY_INTERFACE void flush();
    OSPRAY_INTERFACE void barrier(const Group& group);

    OSPRAY_INTERFACE extern Group world; //! MPI_COMM_WORLD
    OSPRAY_INTERFACE extern Group app; /*! for workers: intracommunicator to app
                        for app: intercommunicator among app processes
                      */
    OSPRAY_INTERFACE extern Group worker; /*!< group of all ospray workers (often the
                           world root is reserved for either app or
                           load balancing, and not part of the worker
                           group */

    OSPRAY_INTERFACE void init(int *ac, const char **av);
  }

} // ::ospray
