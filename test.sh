#extraargs="--silent"
extraargs="--silent --verbose --trace-config all"

printf '\0\0\0\0\0' | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/doesnexist  $extraargs | hexdump -C
printf '\0\0\0\0\0' | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/  $extraargs | hexdump -C
printf '\0\0\0\0\0' | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009  $extraargs | hexdump -C
printf '\0\0\0\0\0' | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/test.yourAPI/whatever_service1  $extraargs | hexdump -C

dd if=/dev/zero bs=5 count=512  | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/test.yourAPI/whatever_service1  $extraargs | hexdump -C

dd if=/dev/zero bs=5 count=512 | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/test.yourAPI/whatever_service1  $extraargs | hexdump -C
dd if=/dev/zero bs=5 count=512 | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/doesntexist  $extraargs | hexdump -C
dd if=/dev/zero bs=5 count=512 | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009  $extraargs | hexdump -C
dd if=/dev/urandom bs=5 count=10 | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009  $extraargs | hexdump -C
dd if=/dev/zero bs=15 count=2048  | curl --http2-prior-knowledge -H 'te: trailers' -H 'content-type: application/grpc' --data-binary @- localhost:19009/test.yourAPI/whatever_service1  $extraargs | hexdump -C
