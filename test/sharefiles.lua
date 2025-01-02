-- example HTTP POST script which demonstrates setting the
-- HTTP method, body, and adding a header

request = function()
    -- print("req")
    wrk.method = "POST"
    -- user: darren；token：浏览器登录时通过F12从浏览器调试窗口获取
    wrk.body = '{"start": 0,"count": 10}'
    wrk.headers["Content-Type"] = "application/json"
    return  wrk.format(wrk.method,"/api/sharefiles&cmd=normal",wrk.headers, wrk.body)
end
response = function(status, headers, body)
    -- print(body) --调试用，正式测试时需要关闭，因为解析response非常消耗资源
end
