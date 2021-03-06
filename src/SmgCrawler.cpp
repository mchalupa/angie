/*******************************************************************************

Copyright (C) 2017 Michal Charvát
Copyright (C) 2017 Michal Kotoun

This file is a part of Angie project.

Angie is free software: you can redistribute it and/or modify it under the
terms of the GNU Lesser General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Angie is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with Angie.  If not, see <http://www.gnu.org/licenses/>.

*******************************************************************************/
/** @file SmgCrawler.cpp */

#include "SmgCrawler.hpp"
#include "Smg/Wrappers.hpp"

using namespace Smg; //TODO: replace with namespace Smg {

void SmgCrawler::CrawlSmg(Object o)
{
  alreadyVisited.clear();
  o.Accept(*this);
}

void SmgCrawler::Visit(HvEdge hve)
{
  GetInnerVisitor().Visit(hve);
}

void SmgCrawler::Visit(PtEdge pte)
{
  GetInnerVisitor().Visit(pte);

  auto object = pte.GetTargetObject();
  if (alreadyVisited.find(object.GetId()) == alreadyVisited.end())
  {
    //have not been here yet
    alreadyVisited.emplace(object.GetId());
    object.Accept(*this);
  }
}

void SmgCrawler::Visit(Object o)
{
  GetInnerVisitor().Visit(o);
  for (auto edge : o.GetOutEdges())
    edge.Accept(*this);
}
void SmgCrawler::Visit(Region r)
{
  GetInnerVisitor().Visit(r);
  for (auto edge : r.GetOutEdges())
    edge.Accept(*this);
}
void SmgCrawler::Visit(Dls s)
{
  GetInnerVisitor().Visit(s);
  for (auto edge : s.GetOutEdges())
    edge.Accept(*this);
}
void SmgCrawler::Visit(Sls s)
{
  GetInnerVisitor().Visit(s);
  for (auto edge : s.GetOutEdges())
    edge.Accept(*this);
}
void SmgCrawler::Visit(Graph g)
{
  GetInnerVisitor().Visit(g);
  for (auto edge : Object(g.graph.handles, g.graph).GetPtOutEdges())
    edge.Accept(*this);
}