#pragma once

#include "Smg/Wrappers.hpp"
#include "Smg/Graph.hpp"
#include "SmgPrinter.hpp"
#include "SmgCrawler.hpp"

#include "OsUtils.hpp"

#if defined(_WIN32)
void ShowSmg(Smg::Impl::Graph& g, bool stack = false, const char* viewer = "explorer");
#else
void ShowSmg(Smg::Impl::Graph& g, bool stack = false, const char* viewer = "xdg-open");
#endif

void PrintDot(Smg::Impl::Graph& g, bool stack = false, const char* file = nullptr);

void ShowSmg(Smg::Impl::Graph& g, bool stack, const char* viewer)
{
  static int printgen = 0;

  SmgPrinter printer{};
  SmgCrawler crawler{printer};
  if (!stack)
    Smg::Graph{g}.Accept(crawler);
  else
    crawler.CrawlSmg(Smg::Object{g.handles,g});
  auto plot_string = printer.GetDot();

  std::string temp = OsUtils::GetEnv("TEMP");
  if (temp.empty())
  {
    temp = OsUtils::GetEnv("PWD");
  }
  std::string dotFileName = temp + PATH_SEPARATOR + "graph" + std::to_string(printgen) + ".dot";
  std::string svgFileName = temp + PATH_SEPARATOR + "graph" + std::to_string(printgen++) + ".svg";

  std::string command =
    "dot -Tsvg -o" + svgFileName + " -Kdot < " + dotFileName  +" && " + viewer + " " + svgFileName;

  OsUtils::WriteToFile(plot_string, dotFileName);
  OsUtils::ExecNoWait(command);
}

void PrintDot(Smg::Impl::Graph& g, bool stack, const char* file)
{
  static int printgen = 0;

  SmgPrinter printer{};
  SmgCrawler crawler{printer};
  if (!stack)
    Smg::Graph{g}.Accept(crawler);
  else
    crawler.CrawlSmg(Smg::Object{g.handles,g});
  auto plot_string = printer.GetDot();

  std::string dotFileName;
  if (file == nullptr)
  {
    std::string temp = OsUtils::GetEnv("TEMP");
    if (temp.empty())
    {
      temp = OsUtils::GetEnv("PWD");
    }
    dotFileName = temp + PATH_SEPARATOR + "graph" + std::to_string(printgen++) + ".dot";
  }
  else
  {
    dotFileName = file;
  }
  OsUtils::WriteToFile(plot_string, dotFileName);
}
