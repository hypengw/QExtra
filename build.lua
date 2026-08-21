local qt = require("lito.qt")

local qextra = lito.target({ kind = "lib", name = "qextra" })
local qt6 = lito.external_dependency(qextra, "qt6")

qt.moc({
  target = qextra,
  qt = qt6,
  files = {
    { source = "src/async.cppm", mode = "module-split", output = "QExtra/async" },
    { source = "src/query.cppm", mode = "module-split", output = "QExtra/query" },
    {
      source = "src/select_storage.cppm",
      mode = "module-split",
      output = "QExtra/select_storage",
    },
    {
      source = "include/kstore/qt/meta_role.hpp",
      mode = "separate",
      output = "kstore/qt/moc_meta_role.cpp",
      compile = false,
    },
    {
      source = "include/kstore/qt/meta_list_model.hpp",
      mode = "separate",
      output = "kstore/qt/moc_meta_list_model.cpp",
      compile = false,
    },
    {
      source = "include/kstore/qt/qtable_proxy_model.hpp",
      mode = "separate",
      output = "kstore/qt/moc_qtable_proxy_model.cpp",
      compile = false,
    },
    {
      source = "include/kstore/qt/qunion_list_model.hpp",
      mode = "separate",
      output = "kstore/qt/moc_qunion_list_model.cpp",
      compile = false,
    },
  },
})
