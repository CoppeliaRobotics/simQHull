#include "simQHull.h"
#include <simLib/simLib.h>
#include <simLib/scriptFunctionData.h>
#include <map>
#include <simMath/3Vector.h>
#include <simMath/4Vector.h>
#include <iostream>

extern "C" {
    #include "mem.h"
    #include "qHull/qset.h"
    #include "libqhull.h"
    #include "poly.h"
}

#define PLUGIN_VERSION 6 // 6 since 4.6

static LIBRARY simLib;

bool compute(const double* verticesIn,int verticesInLength,bool generateIndices,std::vector<double>& verticesOut,std::vector<int>& indicesOut)
{
    coordT* points=new coordT[verticesInLength];
    for (int i=0;i<verticesInLength;i++)
        points[i]=verticesIn[i];
    verticesOut.clear();
    indicesOut.clear();
    char flags[]="qhull QJ";
    int curlong,totlong;     /* memory remaining after qh_memfreeshort */
    FILE *errfile= stderr;    /* error messages from qhull code */
    int exitcode= qh_new_qhull (3,verticesInLength/3,points,0,flags,NULL,errfile);
    if (exitcode==0)
    {
        vertexT* vertex;
        vertexT* vertex0=NULL;
        std::map<int,int> mapping;
        int index=0;
        FORALLvertices
        {
            if (vertex0==NULL)
                vertex0=vertex;
            C3Vector v(vertex->point[0],vertex->point[1],vertex->point[2]);
            verticesOut.push_back(v(0));
            verticesOut.push_back(v(1));
            verticesOut.push_back(v(2));
            mapping[vertex->id]=index++;
        }

        if (generateIndices)
        {
            facetT *facet;
            FORALLfacets
            {
                setT* vertices=facet->vertices;
                index=0;
                while (true)
                {
                    vertexT* v1=(vertexT*)vertices->e[0].p;
                    vertexT* v2=(vertexT*)vertices->e[index+1].p;
                    vertexT* v3=(vertexT*)vertices->e[index+2].p;
                    if (v3==NULL)
                        break;
                    indicesOut.push_back(mapping[v1->id]);
                    indicesOut.push_back(mapping[v2->id]);
                    indicesOut.push_back(mapping[v3->id]);
                    index++;
                }
            }
            // Make sure the normals are all pointing outwards:
            C3Vector center;
            center.clear();
            for (int i=0;i<int(verticesOut.size()/3);i++)
                center+=C3Vector(&verticesOut[3*i]);
            center/=double(verticesOut.size()/3);
            for (int i=0;i<int(indicesOut.size()/3);i++)
            {
                int ind[3]={indicesOut[3*i+0],indicesOut[3*i+1],indicesOut[3*i+2]};
                C3Vector v0(&verticesOut[3*ind[0]]);
                C3Vector v1(&verticesOut[3*ind[1]]);
                C3Vector v2(&verticesOut[3*ind[2]]);
                C3Vector w0(v1-v0);
                C3Vector w1(v2-v0);
                C3Vector n(w0^w1);
                C3Vector m(v0-center);
                if (n*m<0.0f)
                { // flip side
                    indicesOut[3*i+0]=ind[1];
                    indicesOut[3*i+1]=ind[0];
                }
            }
        }
    }
    qh_freeqhull(!qh_ALL);                   /* free long memory  */
    qh_memfreeshort (&curlong, &totlong);    /* free short memory and memory allocator */
    delete[] points;
    if (verticesOut.size()==0)
        return(false);
    return ((indicesOut.size()!=0)||(!generateIndices));
}

// simQHull.compute: deprecated, use sim.getQHull instead
const int inArgs_COMPUTE[]={
    2,
    sim_script_arg_double|sim_script_arg_table,0,
    sim_script_arg_bool,0,
};

void LUA_COMPUTE_CALLBACK(SScriptCallBack* p)
{
    CScriptFunctionData D;
    if (D.readDataFromStack(p->stackID,inArgs_COMPUTE,inArgs_COMPUTE[0],nullptr))
    {
        std::vector<CScriptFunctionDataItem>* inData=D.getInDataPtr();
        double* vertices=&inData->at(0).doubleData[0];
        int verticeslength=inData->at(0).doubleData.size();
        bool generateIndices=inData->at(1).boolData[0];
        if (verticeslength<12)
            simSetLastError(nullptr,"Not enough points specified.");
        else
        {
            double* vOut;
            int vOutL;
            int* iOut;
            int iOutL;
            bool result=simGetQHull(vertices,verticeslength,&vOut,&vOutL,&iOut,&iOutL,0,NULL);
            if (result)
            {
                std::vector<double> verticesOut(vOut,vOut+vOutL);
                std::vector<int> indicesOut(iOut,iOut+iOutL);
                simReleaseBuffer((char*)vOut);
                simReleaseBuffer((char*)iOut);
                D.pushOutData(CScriptFunctionDataItem(verticesOut));
                if (generateIndices)
                    D.pushOutData(CScriptFunctionDataItem(indicesOut));
            }
            else
            { // silent error. We return nothing
            }
        }
    }
    D.writeDataToStack(p->stackID);
}
// --------------------------------------------------------------------------------------

SIM_DLLEXPORT int simInit(SSimInit* info)
{
    simLib=loadSimLibrary(info->coppeliaSimLibPath);
    if (simLib==NULL)
    {
        simAddLog(info->pluginName,sim_verbosity_errors,"could not find or correctly load the CoppeliaSim library. Cannot start the plugin.");
        return(0); 
    }
    if (getSimProcAddresses(simLib)==0)
    {
        simAddLog(info->pluginName,sim_verbosity_errors,"could not find all required functions in the CoppeliaSim library. Cannot start the plugin.");
        unloadSimLibrary(simLib);
        return(0);
    }

    // Register the new function:
    simRegisterScriptCallbackFunction("compute",nullptr,LUA_COMPUTE_CALLBACK);

    return(PLUGIN_VERSION);
}

SIM_DLLEXPORT void simCleanup()
{
    unloadSimLibrary(simLib);
}

SIM_DLLEXPORT void simMsg(SSimMsg*)
{
}

SIM_DLLEXPORT void simQhull(void* data)
{
    // Collect info from CoppeliaSim:
    void** valPtr=(void**)data;
    double* verticesIn=((double*)valPtr[0]);
    int verticesInLength=((int*)valPtr[1])[0];
    bool generateIndices=((bool*)valPtr[2])[0];

    std::vector<double> verticesOut;
    std::vector<int> indicesOut;
    bool result=compute(verticesIn,verticesInLength,generateIndices,verticesOut,indicesOut);
    ((bool*)valPtr[3])[0]=result;
    if (result)
    {
        double* v=(double*)simCreateBuffer(verticesOut.size()*sizeof(double));
        for (size_t i=0;i<verticesOut.size();i++)
            v[i]=verticesOut[i];
        ((double**)valPtr[4])[0]=v;
        ((int*)valPtr[5])[0]=verticesOut.size();
        if (generateIndices)
        {
            int* ind=(int*)simCreateBuffer(indicesOut.size()*sizeof(int));
            for (size_t i=0;i<indicesOut.size();i++)
                ind[i]=indicesOut[i];
            ((int**)valPtr[6])[0]=ind;
            ((int*)valPtr[7])[0]=indicesOut.size();
        }
    }
}

